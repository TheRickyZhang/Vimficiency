-- lua/vimficiency/auto_suggest.lua
-- Surfaces optimizer results without the user calling `:Vimfy end`.
--
-- Suggest sessions are auto-start (from the recall queue) + auto-end,
-- the (auto, auto) cell of the 2×2 session taxonomy. At takeover time
-- the underlying recall record has its `end_kind` flipped from "manual"
-- to "auto" so the finished record reads as a true Suggest, not a
-- Recall that happened to have a result attached.
--
-- Reads `config.auto_suggest` (validated at setup). Three triggers,
-- any subset may be configured:
--
--   idle = { ms, window }
--     Fire after `ms` ms of idleness. Analyzes `window`.
--
--   keys = { every }
--     Fire every `every` real user keystrokes. Analysis slice is the
--     last `every` keystrokes — `window` degenerates to that single
--     value, so there's no separate knob.
--
--   cost = { m, b, ms, window }
--     After `ms` ms of idleness, run the optimizer on
--     `window`; only surface a result when
--     user_cost > m * optimal_cost + b. Idle-gated because the
--     optimizer isn't cheap enough to run per-keystroke.
--
-- Dedup/cooldown: two orthogonal suppressions shared across all three
-- triggers.
--   * cooldown_ms — minimum time between *any* two fires. Enforced
--     per-subscriber by end_trigger (v1 limitation: a fire from one
--     trigger doesn't cool down the others). The shared fingerprint
--     below catches the most common duplicate-output case in practice.
--   * fingerprint — if the computed result matches the last one shown
--     (same edit region + same user keystroke sequence + same top
--     suggestion), silently consume the recall window and skip the
--     notification. Reset on disable().

local M = {}

local config        = require("vimficiency.config")
local end_trigger   = require("vimficiency.end_trigger")
local result_view   = require("vimficiency.result_view")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session_store")

local SUBSCRIBER_IDLE = "auto_suggest_idle"
local SUBSCRIBER_KEYS = "auto_suggest_keys"
local SUBSCRIBER_COST = "auto_suggest_cost"

---@type fun()[]  Disarm handles from every armed trigger. Empty == disabled.
local disarms = {}
---@type string|nil  Fingerprint of the most recent *shown* suggestion.
---                   Shared across all triggers. Reset on disable().
local last_fingerprint = nil

---@param result ResultSession
---@return string
local function fingerprint_result(result)
  -- Including the top optimal result catches the case where the edit
  -- region and user_seq are identical but the surrounding slice has
  -- shifted (different line content -> different optimizer output).
  -- We'd rather re-notify for a truly new recommendation than silently
  -- eat it.
  local first_opt = (result.optimal_results or {})[1]
  local opt_str = first_opt
    and string.format("%s:%.4f", first_opt.seq, first_opt.cost)
    or ""
  return string.format("%d:%d:%d:%d|%s|%s",
    result.start_row, result.start_col, result.end_row, result.end_col,
    result.user_seq or "", opt_str)
end

---@param summary SessionSummary
local function render_suggestion(summary)
  local result = summary.result
  if not result then return end

  local header = "vimficiency suggest "
    .. result_view.format_position(result)
    .. result_view.format_reason_suffix(result)
  local body = result_view.format_body(result)
  vim.notify(header .. "\n" .. table.concat(body, "\n"), vim.log.levels.INFO)
end

--- Shared path for every trigger: resolve the recall window, run the
--- optimizer, and (unless `gate` vetoes) dedup and surface the result.
---
--- Silent on every failure path — auto-fires are speculative, and
--- errors ("queue too young to cover 3s yet", "couldn't snap to a
--- boundary", "optimizer rejected the slice") are expected during
--- normal use.
---
--- Return value is the cooldown signal for end_trigger: true → the
--- fire "counted" (refresh cooldown so we don't re-pay analysis cost
--- on the next tick for the same boundary); false → no-op, don't lock
--- out subsequent fires. We count in every path that actually invoked
--- the optimizer, including dedup skips and gate failures — the point
--- of cooldown is to avoid re-running analysis on the same boundary.
---
---@param window string                              Recall alias to analyze.
---@param gate fun(result: ResultSession): boolean|nil  Optional veto. Return false to suppress notification; the record is still consumed.
---@param reason FinishReason                         Which trigger fired; stored on the result for the header annotation.
---@return boolean counted
local function fire_with_window(window, gate, reason)
  if not session_store.is_recall_enabled() then return false end

  local active = session_store.get_active(window)
  if not active then return false end  -- queue too young or no clean boundary

  -- Pin the resolved id. Every store mutation below must key on this
  -- id, never re-resolve `window`: the recall alias is time-varying and
  -- may slide to a different record between get_active and finish.
  local id = active.id
  local result, _err = session.compute_result_for_active(active)
  if not result then
    -- The optimizer actually ran (or rejected the slice). Throttle
    -- repeated failures on the same window. NOTE: end_kind is NOT
    -- mutated here — a failed compute leaves the record a Recall.
    return true
  end

  -- Gate check (cost trigger): if the user was efficient on this
  -- window, don't notify. Still consume the record so subsequent
  -- ticks don't re-pay the analysis cost on the same boundary.
  if gate and not gate(result) then
    session_store.finish_session(id, result, window, "auto", reason)
    return true
  end

  local fp = fingerprint_result(result)
  if fp == last_fingerprint then
    -- Silently consume this exact record so we stop re-analyzing it.
    session_store.finish_session(id, result, window, "auto", reason)
    return true
  end

  -- Promote Recall -> Suggest by passing the end_kind override. The
  -- mutation happens atomically with the status transition inside
  -- finish_session, so a finish that returns false leaves end_kind
  -- untouched — never a mislabeled Suggest on a record that didn't
  -- actually finish.
  if not session_store.finish_session(id, result, window, "auto", reason) then
    return true
  end

  last_fingerprint = fp

  local summary = session_store.summarize(id)
  if summary then render_suggestion(summary) end
  return true
end

---@return boolean counted
local function fire_idle()
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return false end
  return fire_with_window(cfg.idle.window, nil, "suggest_idle")
end

---@return boolean counted
local function fire_keys()
  local cfg = config.auto_suggest
  if not cfg or not cfg.keys then return false end
  -- `every` is the analysis slice: "surface suggestions based on the
  -- last N keystrokes, every N keystrokes." A separate window knob
  -- would just collapse to this same value.
  return fire_with_window(tostring(cfg.keys.every), nil, "suggest_keys")
end

---@return boolean counted
local function fire_cost()
  local cfg = config.auto_suggest
  if not cfg or not cfg.cost then return false end
  local m, b = cfg.cost.m, cfg.cost.b
  return fire_with_window(cfg.cost.window, function(result)
    local optimal = (result.optimal_results or {})[1]
    if not optimal then return false end
    local user_cost = result.user_cost or 0
    return user_cost > m * optimal.cost + b
  end, "suggest_cost")
end

--- Turn on auto-suggest. Fails silently if `config.auto_suggest` has no
--- triggers configured — `suggest on` without a config is a no-op, and
--- the :Vimfy layer surfaces that as an error to the user.
---@return boolean success
function M.enable()
  if #disarms > 0 then return false end
  local cfg = config.auto_suggest
  if not cfg then return false end

  local cooldown_ms = cfg.cooldown_ms or 0

  if cfg.idle then
    local d = end_trigger.arm_idle({
      name        = SUBSCRIBER_IDLE,
      idle_ms     = cfg.idle.ms,
      cooldown_ms = cooldown_ms,
      on_fire     = fire_idle,
    })
    if d then table.insert(disarms, d) end
  end

  if cfg.keys then
    local d = end_trigger.arm_keys({
      name        = SUBSCRIBER_KEYS,
      every       = cfg.keys.every,
      cooldown_ms = cooldown_ms,
      on_fire     = fire_keys,
    })
    if d then table.insert(disarms, d) end
  end

  if cfg.cost then
    -- Cost uses an idle trigger at `cost.ms` cadence. v1 runs the
    -- optimizer synchronously on the idle tick; if this ever shows up
    -- as a measured cost, the debounced-subprocess plan in the ADR
    -- kicks in.
    local d = end_trigger.arm_idle({
      name        = SUBSCRIBER_COST,
      idle_ms     = cfg.cost.ms,
      cooldown_ms = cooldown_ms,
      on_fire     = fire_cost,
    })
    if d then table.insert(disarms, d) end
  end

  return #disarms > 0
end

function M.disable()
  if #disarms == 0 then return end
  for _, d in ipairs(disarms) do d() end
  disarms = {}
  last_fingerprint = nil
end

---@return boolean
function M.is_enabled()
  return #disarms > 0
end

--- True iff at least one trigger is configured. Lets the :Vimfy layer
--- give a friendly error when the user runs `:Vimfy suggest on` with
--- no config.
---@return boolean
function M.is_configured()
  local cfg = config.auto_suggest
  if not cfg then return false end
  return cfg.idle ~= nil or cfg.keys ~= nil or cfg.cost ~= nil
end

-- Test-only exports of module-local state. Not part of the public API.
M._for_test = {
  fire_idle = fire_idle,
  fire_keys = fire_keys,
  fire_cost = fire_cost,
  get_last_fingerprint = function() return last_fingerprint end,
  reset = function()
    last_fingerprint = nil
  end,
}

return M
