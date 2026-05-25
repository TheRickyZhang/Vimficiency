-- Auto-suggest surfaces optimizer results without an explicit `:Vimfy finish`.
-- It reads `config.auto_suggest` and arms any configured idle/keys/cost triggers.

local M = {}

local config        = require("vimficiency.config")
local end_trigger   = require("vimficiency.capture.end_trigger")
local result_view   = require("vimficiency.session.result_view")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

local SUBSCRIBER_IDLE = "auto_suggest_idle"
local SUBSCRIBER_KEYS = "auto_suggest_keys"
local SUBSCRIBER_COST = "auto_suggest_cost"

---@type fun()[]  Disarm handles from every armed trigger. Empty == disabled.
local disarms = {}
---@type string|nil  Fingerprint of the most recent shown suggestion.
local last_fingerprint = nil

---@param result VF.Session.Result
---@return string
local function fingerprint_result(result)
  -- Include the top result so slice changes still count as new suggestions.
  local first_opt = (result.optimal_results or {})[1]
  local opt_str = first_opt
    and string.format("%s:%.4f", first_opt.seq, first_opt.cost)
    or ""
  return string.format("%d:%d:%d:%d|%s|%s",
    result.start_row, result.start_col, result.end_row, result.end_col,
    result.user_seq or "", opt_str)
end

---@param summary VF.Session.Summary
local function render_suggestion(summary)
  local result = summary.result
  if not result then return end

  vim.notify(
    result_view.format_message("vimfy suggest", result),
    vim.log.levels.INFO)
end

--- Shared path for every trigger.
--- Returns the cooldown signal for `end_trigger`.
---
---@param window string                              Recall alias to analyze.
---@param gate? fun(result: VF.Session.Result): boolean|nil  Optional veto. Return false to suppress notification; the record is still consumed.
---@param reason VF.Session.FinishReason                         Which trigger fired; stored on the result for the header annotation.
---@return boolean counted
local function fire_with_window(window, gate, reason)
  local active = session_store.get_active(window)
  if not active then return false end  -- queue too young or no clean boundary

  -- Pin the resolved id; recall aliases are time-varying.
  local id = active.id
  local result = session.compute_result_for_active(active)
  if not result then
    return true
  end

  -- Cost gate: consume the record but suppress the notification.
  if gate and not gate(result) then
    session_store.finish_session(id, result, window, "auto", reason)
    return true
  end

  local fp = fingerprint_result(result)
  if fp == last_fingerprint then
    session_store.finish_session(id, result, window, "auto", reason)
    return true
  end

  -- Promote Recall -> Suggest with the finish-time end_kind override.
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

--- Turn on auto-suggest.
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

--- True iff at least one trigger is configured.
---@return boolean
function M.is_configured()
  local cfg = config.auto_suggest
  if not cfg then return false end
  return cfg.idle ~= nil or cfg.keys ~= nil or cfg.cost ~= nil
end

-- Test-only exports.
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
