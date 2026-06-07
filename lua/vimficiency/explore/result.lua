local ffi_explore = require("vimficiency.ffi.explore")

local M = {}
local v = vim.api

---@class VF.Explore.OpenResult : VF.Session.Result
---@field goal_lines string[]
---@field has_lines_above boolean
---@field has_lines_below boolean
---@field user_seq string
---@field user_cost number
---@field optimal_results VF.Optimizer.Result[]

local REQUIRED = {
  { key = "lines", kind = "table" },
  { key = "goal_lines", kind = "table" },
  { key = "start_row", kind = "number" },
  { key = "start_col", kind = "number" },
  { key = "end_row", kind = "number" },
  { key = "end_col", kind = "number" },
  { key = "has_lines_above", kind = "boolean" },
  { key = "has_lines_below", kind = "boolean" },
  { key = "user_seq", kind = "string" },
  { key = "user_cost", kind = "number" },
  { key = "optimal_results", kind = "table" },
}

---@param result any
---@return boolean ok
---@return string|nil err
function M.validate(result)
  if type(result) ~= "table" then
    return false, "result must be a table"
  end
  for _, field in ipairs(REQUIRED) do
    if type(result[field.key]) ~= field.kind then
      return false, string.format(
        "result.%s must be %s, got %s",
        field.key, field.kind, type(result[field.key]))
    end
  end
  if #result.lines == 0 then return false, "result.lines must be nonempty" end
  if #result.goal_lines == 0 then return false, "result.goal_lines must be nonempty" end
  return true, nil
end

---@param result VF.Explore.OpenResult
---@return integer
function M.boundary_last_col(result)
  local initial_last = result.lines[#result.lines]
  local goal_last = result.goal_lines[#result.goal_lines]
  return math.max(0, math.max(#initial_last, #goal_last) - 1)
end

---@param result VF.Explore.OpenResult
---@param source_win integer
---@param optimizer_overrides string|nil
---Returns a job id; the composition search runs on a worker thread. Poll with
---`ffi_explore.explore_poll` until it yields a view id.
---@return integer
function M.start_view_async(result, source_win, optimizer_overrides, deadline_ms)
  return ffi_explore.explore_start_async(
    result.lines, result.start_row, result.start_col,
    result.goal_lines, result.end_row, result.end_col,
    0, M.boundary_last_col(result),
    result.has_lines_above, result.has_lines_below,
    v.nvim_win_get_height(source_win),
    v.nvim_get_option_value("scroll", { win = source_win }),
    result.user_seq,
    optimizer_overrides,
    deadline_ms)
end

return M
