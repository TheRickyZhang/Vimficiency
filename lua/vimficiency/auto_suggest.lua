-- lua/vimficiency/auto_suggest.lua
-- Surfaces optimizer results without the user calling `:Vimfy end`.
--
-- Suggest sessions are auto-start (from the recall queue) + auto-end
-- (idle trigger), the (auto, auto) cell of the 2×2 session taxonomy. At
-- takeover time the underlying recall record has its `end_kind` flipped
-- from "manual" to "auto" so the finished record reads as a true
-- Suggest, not a Recall that happened to have a result attached.
--
-- Reads `config.auto_suggest` (validated at setup). Currently one
-- trigger is implemented:
--
--   idle = { ms = N, window = "Ns" | "N" }
--     Fire after `ms` milliseconds of no keystroke activity. Analyzes
--     the given recall window and displays the result.
--
-- Reserved (not yet implemented): `keys`, `cost`.
--
-- Dedup/cooldown: two orthogonal suppressions.
--   * cooldown_ms — minimum time between *any* two fires. Enforced by
--     end_trigger.arm_idle.
--   * fingerprint — if the computed result matches the last one shown
--     (same edit region + same user keystroke sequence), silently
--     consume the recall window and skip the notification. The user
--     already saw this suggestion; re-showing it on every idle tick
--     would be spam.

local M = {}

local config        = require("vimficiency.config")
local end_trigger   = require("vimficiency.end_trigger")
local result_view   = require("vimficiency.result_view")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session_store")

local SUBSCRIBER_NAME = "auto_suggest_idle"

---@type fun()|nil  Disarm handle from end_trigger.arm_idle.
local disarm = nil
---@type string|nil  Fingerprint of the most recent *shown* suggestion.
---                   Reset on disable(); not on cooldown-only skips.
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

  local header = "vimficiency suggest " .. result_view.format_position(result)
  local body = result_view.format_body(result)
  vim.notify(header .. "\n" .. table.concat(body, "\n"), vim.log.levels.INFO)
end

--- Run the optimizer on the configured idle window and surface the result.
--- Silent on every failure path: idle-fires are speculative, and errors
--- ("queue too young to cover 3s yet", "couldn't snap to a boundary",
--- "optimizer rejected the slice") are expected during normal use.
---
--- Return value is the cooldown signal for end_trigger: true → the fire
--- "counted" (refresh cooldown so we don't re-pay analysis cost on the
--- next idle tick for the same boundary); false → no-op, don't lock out
--- subsequent fires.
---@return boolean counted
local function fire_idle()
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return false end

  if not session_store.is_recall_enabled() then return false end

  local window = cfg.idle.window
  local active = session_store.get_active(window)
  if not active then return false end  -- queue too young or no clean boundary

  -- Pin the resolved id. Every store mutation below must key on this
  -- id, never re-resolve `window`: the recall alias is time-varying and
  -- may slide to a different record between get_active and finish.
  -- Otherwise we'd attach the result we just computed to the wrong
  -- session, and finish a different one than we analyzed.
  local id = active.id
  local result, _err = session.compute_result_for_active(active)
  if not result then
    -- The optimizer actually ran (or rejected the slice). Throttle
    -- repeated failures on the same window — otherwise every idle tick
    -- re-pays the analysis cost for a boundary that isn't going to
    -- improve on the next 200 ms.
    return true
  end

  -- Promote Recall -> Suggest: the record was born as (auto, manual)
  -- when the recall queue created it; now that an auto end-trigger is
  -- finishing it, flip end_kind so the taxonomy reads truthfully.
  active.end_kind = "auto"

  local fp = fingerprint_result(result)
  if fp == last_fingerprint then
    -- Silently consume this exact record so we stop re-analyzing it,
    -- refresh the cooldown, and skip the notification. Finishing here
    -- is safe: the session's result is preserved on the record.
    session_store.finish_session(id, result, window)
    return true
  end

  if not session_store.finish_session(id, result, window) then
    return true
  end

  last_fingerprint = fp

  local summary = session_store.summarize(id)
  if summary then render_suggestion(summary) end
  return true
end

--- Turn on auto-suggest. Fails silently if `config.auto_suggest` has no
--- triggers configured — `suggest on` without a config is a no-op, and
--- the :Vimfy layer surfaces that as an error to the user.
---@return boolean success
function M.enable()
  if disarm then return false end
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return false end

  disarm = end_trigger.arm_idle({
    name        = SUBSCRIBER_NAME,
    idle_ms     = cfg.idle.ms,
    cooldown_ms = cfg.cooldown_ms or 0,
    on_fire     = fire_idle,
  })
  return disarm ~= nil
end

function M.disable()
  if not disarm then return end
  disarm()
  disarm = nil
  last_fingerprint = nil
end

---@return boolean
function M.is_enabled()
  return disarm ~= nil
end

--- True iff there's a trigger configured. Lets the :Vimfy layer give a
--- friendly error when the user runs `:Vimfy suggest on` with no config.
---@return boolean
function M.is_configured()
  local cfg = config.auto_suggest
  return cfg and cfg.idle ~= nil or false
end

-- Test-only exports of module-local state. Not part of the public API;
-- used by tests/lua/test_auto_suggest.lua to drive fire_idle under
-- monkey-patched dependencies and inspect state.
M._for_test = {
  fire_idle = fire_idle,
  get_last_fingerprint = function() return last_fingerprint end,
  reset = function()
    last_fingerprint = nil
  end,
}

return M
