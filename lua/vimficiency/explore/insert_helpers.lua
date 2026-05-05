local v = vim.api

local M = {}

---@param a VF.Explore.Active
---@return string
function M.current_remaining(a)
  local pending = a.pending
  if not pending then return "" end
  local target = pending.target
  local cursor = v.nvim_win_get_cursor(a.scratch.win)
  local cur_row, cur_col = cursor[1] - 1, cursor[2]
  if cur_row ~= pending.row or cur_col < pending.col_start then return target end
  local line = assert(v.nvim_buf_get_lines(a.scratch.buf, pending.row, pending.row + 1, false)[1],
    "vimfy explore: pending insert row missing")
  local typed_so_far = line:sub(pending.col_start + 1, cur_col)
  if target:sub(1, #typed_so_far) == typed_so_far then
    return target:sub(#typed_so_far + 1)
  end
  return target
end

return M
