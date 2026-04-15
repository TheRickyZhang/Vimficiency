-- lua/vimficiency/end_trigger.lua
-- Shared auto-fire engine for auto-end features. Two trigger shapes:
--
--   * arm_idle  — "fire after N ms of global keystroke idleness."
--   * arm_keys  — "fire every N real user keystrokes."
--
-- Used by:
--   * auto_suggest.lua  — Suggest sessions (auto start, auto end).
--   * session.lua       — Watch sessions (manual start, auto end).
--
-- A single arm_* call attaches one named global key subscriber plus
-- (for arm_idle) one uv timer. Multiple concurrent armings are supported
-- as long as each uses a distinct `name` (enforced by
-- key_tracking.attach_global). Distinct names between arm_idle and
-- arm_keys are the caller's responsibility.
--
-- Cooldown is cooperative: the `on_fire` callback returns truthy if the
-- fire counted (refresh cooldown), falsy if it was a no-op (no refresh).
-- This matches auto_suggest's needs: a "ring too young" skip shouldn't
-- lock out the next idle tick, but an analysis that actually ran should.
--
-- Each arm holds its own cooldown clock (per-subscriber). When multiple
-- triggers from the same feature are armed concurrently (e.g. Suggest's
-- idle + keys + cost), a fire from one does NOT cool down the others —
-- feature-wide mutual cooldown is a v2 refinement. The fingerprint dedup
-- inside auto_suggest.fire_* catches the most common duplicate-output
-- case in practice.

local M = {}

local key_tracking = require("vimficiency.key_tracking")

---@class EndTriggerOpts
---@field name         string                       # Unique subscriber name.
---@field idle_ms      integer                      # Idle threshold before fire.
---@field cooldown_ms  integer                      # Minimum time between fires.
---@field on_fire      fun(): boolean?              # Callback. Truthy → refresh cooldown.

--- Arm an idle trigger. Returns a disarm function; returns nil if the
--- subscriber name is already taken.
---@param opts EndTriggerOpts
---@return fun()|nil disarm
function M.arm_idle(opts)
  assert(type(opts.name) == "string" and #opts.name > 0, "end_trigger: name required")
  assert(type(opts.idle_ms) == "number" and opts.idle_ms > 0, "end_trigger: idle_ms > 0 required")
  assert(type(opts.cooldown_ms) == "number" and opts.cooldown_ms >= 0, "end_trigger: cooldown_ms >= 0 required")
  assert(type(opts.on_fire) == "function", "end_trigger: on_fire required")

  local idle_ms     = opts.idle_ms
  local cooldown_ns = opts.cooldown_ms * 1e6
  local on_fire     = opts.on_fire
  local name        = opts.name

  local timer = vim.uv.new_timer()
  if not timer then return nil end

  local disarmed = false
  local last_fire_hrtime = 0

  local function fire()
    if disarmed then return end
    local now_ns = vim.uv.hrtime()
    if last_fire_hrtime > 0 and (now_ns - last_fire_hrtime) < cooldown_ns then
      return
    end
    local counted = on_fire()
    if counted then
      last_fire_hrtime = now_ns
    end
  end

  local function reset_timer()
    if disarmed then return end
    timer:stop()
    timer:start(idle_ms, 0, vim.schedule_wrap(fire))
  end

  local ok = key_tracking.attach_global(function(_event)
    reset_timer()
  end, name)
  if not ok then
    timer:close()
    return nil
  end

  return function()
    if disarmed then return end
    disarmed = true
    key_tracking.detach_global(name)
    timer:stop()
    timer:close()
  end
end

---@class ArmKeysOpts
---@field name         string                       # Unique subscriber name.
---@field every        integer                      # Fire every N real keystrokes.
---@field cooldown_ms  integer                      # Minimum time between fires.
---@field on_fire      fun(): boolean?              # Callback. Truthy → refresh cooldown.

--- Arm a keystroke-count trigger. Fires `on_fire` every `every` user
--- keystrokes that pass `key_tracking`'s admin/cmdline filters.
---
--- The counter is reset on every fire regardless of cooldown: if `every`
--- keystrokes accrue while the cooldown is still active, the subscriber
--- silently consumes them (no fire, counter resets). This matches the
--- idle-trigger semantic: cooldown suppresses fires, it doesn't buffer
--- them into a queue that later spams the user.
---
--- Returns a disarm function; returns nil if the subscriber name is
--- already taken.
---@param opts ArmKeysOpts
---@return fun()|nil disarm
function M.arm_keys(opts)
  assert(type(opts.name) == "string" and #opts.name > 0, "end_trigger: name required")
  assert(type(opts.every) == "number" and opts.every > 0, "end_trigger: every > 0 required")
  assert(type(opts.cooldown_ms) == "number" and opts.cooldown_ms >= 0, "end_trigger: cooldown_ms >= 0 required")
  assert(type(opts.on_fire) == "function", "end_trigger: on_fire required")

  local every       = opts.every
  local cooldown_ns = opts.cooldown_ms * 1e6
  local on_fire     = opts.on_fire
  local name        = opts.name

  local disarmed = false
  local count = 0
  local last_fire_hrtime = 0

  local ok = key_tracking.attach_global(function(_event)
    if disarmed then return end
    count = count + 1
    if count < every then return end
    count = 0
    local now_ns = vim.uv.hrtime()
    if last_fire_hrtime > 0 and (now_ns - last_fire_hrtime) < cooldown_ns then
      return
    end
    -- Defer to the main loop so on_fire can safely touch Neovim API.
    vim.schedule(function()
      if disarmed then return end
      local counted = on_fire()
      if counted then
        last_fire_hrtime = vim.uv.hrtime()
      end
    end)
  end, name)
  if not ok then return nil end

  return function()
    if disarmed then return end
    disarmed = true
    key_tracking.detach_global(name)
  end
end

return M
