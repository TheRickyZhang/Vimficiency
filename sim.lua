local v = vim.api
local cmd = vim.cmd

local M = {}
local global_buf = nil

local function get_buf()
  if global_buf and v.nvim_buf_is_valid(global_buf) then
    return global_buf
  end
  -- Temporary buffer
  local cbuf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_name(cbuf, "temp buffer")
  v.nvim_set_option_value("buftype",   "nofile", { buf = cbuf })
  v.nvim_set_option_value("bufhidden", "hide",   { buf = cbuf })
  v.nvim_set_option_value("swapfile",  false,    { buf = cbuf, scope = "local" })
  global_buf = cbuf
  return global_buf
end

local function get_win(buf)
  for _, win in ipairs(v.nvim_list_wins()) do
    if v.nvim_win_get_buf(win) == buf then
      return win
    end
  end
  local old_win = v.nvim_get_current_win()
  cmd("vsplit")
  local sim_win = v.nvim_get_current_win()
  v.nvim_win_set_buf(sim_win, buf)
  v.nvim_set_current_win(old_win)
  return sim_win
end

function M.simulate_visible(starting_lines, row, col, keys)
  print("trying to simulate" .. keys)
  local buf = get_buf()
  local win = get_win(buf)
  local old_win = v.nvim_get_current_win()

  -- Replace buffer content with starting lines
  v.nvim_buf_set_lines(buf, 0, -1, true, starting_lines)
  v.nvim_win_set_cursor(win, {row, col})
  v.nvim_set_current_win(win)

  -- replay keys
  cmd("normal! " .. keys)

  v.nvim_set_current_win(old_win)
  print("done")
  return nil
end

return M
