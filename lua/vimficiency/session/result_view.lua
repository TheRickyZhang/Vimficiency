-- Pure formatting helpers for VF.Session.Result display.

local sequence_display = require("vimficiency.sequence_display")

local M = {}

---@param result VF.Session.Result
---@return string  e.g. "(1,2) → (3,4)"
function M.format_position(result)
  return string.format("(%d,%d) → (%d,%d)",
    result.start_row, result.start_col, result.end_row, result.end_col)
end

---@param result VF.Session.Result
---@return string  e.g. " [manual]", or ""
function M.format_reason_suffix(result)
  if not result.finish_reason then return "" end
  return " [" .. result.finish_reason .. "]"
end

--- Format the result body as display lines.
---@param result VF.Session.Result
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

---@param title string
---@param result VF.Session.Result
---@return string
function M.format_message(title, result)
  local body = M.format_body(result)
  return title
    .. " " .. M.format_position(result)
    .. M.format_reason_suffix(result)
    .. "\n" .. table.concat(body, "\n")
end

---@param name string
---@param result VF.Session.Result
---@return string[]
function M.format_saved_lines(name, result)
  local user_cost_str = result.user_cost
    and string.format(" (cost: %.2f)", result.user_cost) or ""
  local output_lines = {
    "=== " .. name .. " ===",
    "",
    string.format("Position: (%d, %d) -> (%d, %d)",
      result.start_row, result.start_col,
      result.end_row, result.end_col),
    "",
  }

  if result.user_seq and result.user_seq ~= "" then
    vim.list_extend(output_lines,
      sequence_display.prefixed_lines("User sequence: ", result.user_seq, nil, user_cost_str))
  else
    table.insert(output_lines, "User sequence: (none)" .. user_cost_str)
  end

  table.insert(output_lines, "")
  table.insert(output_lines, "Recommendations:")

  local optimal = result.optimal_results or {}
  for i, r in ipairs(optimal) do
    vim.list_extend(output_lines,
      sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq, nil,
        string.format(" (cost: %.2f)", r.cost or 0)))
  end

  if #optimal == 0 then
    table.insert(output_lines, "  (no results)")
  end

  table.insert(output_lines, "")
  table.insert(output_lines, "Buffer context:")
  for i, line in ipairs(result.lines or {}) do
    local row = i - 1
    local prefix = "  "
    if row == result.start_row then
      prefix = "> "
    elseif row == result.end_row then
      prefix = "< "
    end
    table.insert(output_lines, prefix .. line)
  end

  return output_lines
end

return M
