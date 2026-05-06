local v = vim.api
local insert_repair = require("vimficiency.explore.insert_repair")

local M = {}

---@param a VF.Explore.Active
---@return string|nil literal_target
---@return string typed_so_far
local function pending_typed_so_far(a)
  local pending = a.pending
  if not pending then return nil, "" end
  local literal_target = pending.literal_target or pending.target
  local cursor = v.nvim_win_get_cursor(a.scratch.win)
  local cur_row, cur_col = cursor[1] - 1, cursor[2]
  if cur_row ~= pending.row or cur_col < pending.col_start then
    return literal_target, ""
  end
  local line = assert(v.nvim_buf_get_lines(a.scratch.buf, pending.row, pending.row + 1, false)[1],
    "vimfy explore: pending insert row missing")
  return literal_target, line:sub(pending.col_start + 1, cur_col)
end

---@param a VF.Explore.Active
---@return string
function M.current_typed(a)
  local _, typed_so_far = pending_typed_so_far(a)
  return typed_so_far
end

---@param a VF.Explore.Active
---@return VF.Explore.InsertContinuation
function M.current_continuation(a)
  local literal_target, typed_so_far = pending_typed_so_far(a)
  if not literal_target then return insert_repair.empty() end
  return insert_repair.plan(literal_target, typed_so_far)
end

return M
