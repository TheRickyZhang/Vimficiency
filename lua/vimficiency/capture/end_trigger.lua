-- Shared auto-fire engine for watch and auto_suggest.
-- Supports idle triggers and keystroke-count triggers.

local M = {}

local key_tracking = require("vimficiency.capture.key_tracking")

---@class VF.Capture.EndTriggerOpts
---@field name         string                       # Unique subscriber name.
---@field idle_ms      integer                      # Idle threshold before fire.
---@field cooldown_ms  integer                      # Minimum time between fires.
---@field on_fire      fun(): boolean?              # Callback. Truthy → refresh cooldown.

--- Arm an idle trigger.
---@param opts VF.Capture.EndTriggerOpts
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

  local ok = key_tracking.attach_global(function()
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

---@class VF.Capture.ArmKeysOpts
---@field name         string                       # Unique subscriber name.
---@field every        integer                      # Fire every N real keystrokes.
---@field cooldown_ms  integer                      # Minimum time between fires.
---@field on_fire      fun(): boolean?              # Callback. Truthy → refresh cooldown.

--- Arm a keystroke-count trigger.
---@param opts VF.Capture.ArmKeysOpts
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

  local ok = key_tracking.attach_global(function()
    if disarmed then return end
    count = count + 1
    if count < every then return end
    count = 0
    local now_ns = vim.uv.hrtime()
    if last_fire_hrtime > 0 and (now_ns - last_fire_hrtime) < cooldown_ns then
      return
    end
    -- Defer so `on_fire` can safely touch Neovim API.
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
