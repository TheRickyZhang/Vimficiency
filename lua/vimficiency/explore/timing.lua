local M = {}

-- Threshold (ms) above which an explore compute is surfaced, for measuring the
-- per-keystroke recommendations path against the interactive budget. Override
-- via vim.g.vimficiency_explore_slow_ms.
local DEFAULT_SLOW_MS = 100

local function threshold_ms()
  return tonumber(vim.g.vimficiency_explore_slow_ms) or DEFAULT_SLOW_MS
end

---@param label string
---@param ms number
function M.note(label, ms)
  if ms < threshold_ms() then return end
  vim.schedule(function()
    vim.notify(string.format("vimfy explore: %s ran %.0fms", label, ms),
      vim.log.levels.WARN)
  end)
end

return M
