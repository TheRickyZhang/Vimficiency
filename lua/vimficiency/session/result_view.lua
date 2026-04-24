-- Pure formatting helpers for ResultSession display.

local sequence_display = require("vimficiency.sequence_display")

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
    vim.list_extend(lines,
      sequence_display.prefixed_lines("  user: ", result.user_seq, nil, cost_str))
  end
  for i, r in ipairs(result.optimal_results or {}) do
    vim.list_extend(lines,
      sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq, nil,
        string.format(" (%.2f)", r.cost)))
  end
  return lines
end

return M
