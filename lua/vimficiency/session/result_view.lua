-- Pure formatting helpers for ResultSession display.

local ffi_lib = require("vimficiency.ffi")

local M = {}

---@param result ResultSession
---@return string  e.g. "(1,2) → (3,4)"
function M.format_position(result)
  return string.format("(%d,%d) → (%d,%d)",
    result.start_row, result.start_col, result.end_row, result.end_col)
end

---@param result ResultSession
---@return string  e.g. " [manual]", or ""
function M.format_reason_suffix(result)
  if not result.finish_reason then return "" end
  return " [" .. result.finish_reason .. "]"
end

--- Format the result body as display lines.
---@param result ResultSession
---@return string[]
function M.format_body(result)
  local lines = {}
  if result.user_seq and result.user_seq ~= "" then
    local cost_str = result.user_cost
      and string.format(" (%.2f)", result.user_cost) or ""
    table.insert(lines, string.format("  user: %s%s",
      ffi_lib.format_sequence(result.user_seq), cost_str))
  end
  for i, r in ipairs(result.optimal_results or {}) do
    table.insert(lines, string.format("  %d. %s (%.2f)",
      i, ffi_lib.format_sequence(r.seq), r.cost))
  end
  return lines
end

return M
