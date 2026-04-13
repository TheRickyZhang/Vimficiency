-- lua/vimficiency/auto_suggest.lua
-- Surfaces optimizer results without the user calling `:Vimfy end`.
--
-- Reads `config.auto_suggest` (validated at setup) and registers the
-- configured triggers. Currently one trigger is implemented:
--
--   idle = { ms = N, window = "Ns" | "N" }
--     Fire after `ms` milliseconds of no keystroke activity. Analyzes
--     the given recall window and displays the result.
--
-- Reserved (not yet implemented): `keys`, `cost`.
--
-- Dedup/cooldown: two orthogonal suppressions.
--   * cooldown_ms — minimum time between *any* two fires.
--   * fingerprint — if the computed result matches the last one shown
--     (same edit region + same user keystroke sequence), silently
--     consume the recall window and skip the notification. The user
--     already saw this suggestion; re-showing it on every idle tick
--     would be spam.

local M = {}

local config        = require("vimficiency.config")
local key_tracking  = require("vimficiency.key_tracking")
local result_view   = require("vimficiency.result_view")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session_store")

local SUBSCRIBER_NAME = "auto_suggest_idle"

local enabled = false
---@type userdata|nil  vim.uv timer handle
local idle_timer = nil
local last_fire_hrtime = 0
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
--- ("ring too young to cover 3s yet", "couldn't snap to a boundary",
--- "optimizer rejected the slice") are expected during normal use.
local function fire_idle()
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return end

  if not session_store.is_recall_enabled() then return end

  local now_ns = vim.uv.hrtime()
  local cooldown_ns = (cfg.cooldown_ms or 0) * 1e6
  if last_fire_hrtime > 0 and (now_ns - last_fire_hrtime) < cooldown_ns then
    return
  end

  local window = cfg.idle.window
  local active = session_store.get_active(window)
  if not active then return end  -- ring too young or no clean boundary

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
    last_fire_hrtime = now_ns
    return
  end

  local fp = fingerprint_result(result)
  if fp == last_fingerprint then
    -- Silently consume this exact record so we stop re-analyzing it,
    -- refresh the cooldown, and skip the notification. Finishing here
    -- is safe: the session's result is preserved on the record.
    if session_store.finish_session(id, result) then
      last_fire_hrtime = now_ns
    end
    return
  end

  if not session_store.finish_session(id, result) then
    last_fire_hrtime = now_ns
    return
  end

  last_fire_hrtime = now_ns
  last_fingerprint = fp

  local summary = session_store.summarize(id)
  if summary then render_suggestion(summary) end
end

--- Reset the idle timer. Called on every real (non-admin) keystroke via
--- key_tracking's global subscriber, which already skips events while the
--- `ignoring` flag is set.
local function reset_idle_timer()
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return end
  if not idle_timer then
    idle_timer = vim.uv.new_timer()
    if not idle_timer then return end
  end
  idle_timer:stop()
  idle_timer:start(cfg.idle.ms, 0, vim.schedule_wrap(fire_idle))
end

--- Turn on auto-suggest. Fails silently if `config.auto_suggest` has no
--- triggers configured — `suggest on` without a config is a no-op, and
--- the :Vimfy layer surfaces that as an error to the user.
---@return boolean success
function M.enable()
  if enabled then return false end
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return false end

  local ok = key_tracking.attach_global(function(_event)
    reset_idle_timer()
  end, SUBSCRIBER_NAME)
  if not ok then return false end

  enabled = true
  return true
end

function M.disable()
  if not enabled then return end
  key_tracking.detach_global(SUBSCRIBER_NAME)
  if idle_timer then
    idle_timer:stop()
  end
  last_fire_hrtime = 0
  last_fingerprint = nil
  enabled = false
end

---@return boolean
function M.is_enabled()
  return enabled
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
-- monkey-patched dependencies and inspect cooldown state.
M._for_test = {
  fire_idle = fire_idle,
  get_last_fire_hrtime = function() return last_fire_hrtime end,
  set_last_fire_hrtime = function(v) last_fire_hrtime = v end,
  get_last_fingerprint = function() return last_fingerprint end,
  reset = function()
    last_fire_hrtime = 0
    last_fingerprint = nil
  end,
}

return M
