local v          = vim.api
local util       = require("vimficiency.util")
local buf_window = require("vimficiency.explore.buf_window")

local M = {}

-- Run `cmd` (a window-split command) and return the new window — i.e.,
-- the one in the current tab that wasn't there before.
local function split_and_capture(cmd)
  local tab = v.nvim_get_current_tabpage()
  local before = {}
  for _, w in ipairs(v.nvim_tabpage_list_wins(tab)) do before[w] = true end
  vim.cmd(cmd)
  for _, w in ipairs(v.nvim_tabpage_list_wins(tab)) do
    if not before[w] then return w end
  end
  error("vimfy explore: split " .. cmd .. " did not create a new window")
end

---@class VF.Explore.Layout
---@field scratch_buf integer
---@field scratch_win integer
---@field scratch_tab integer
---@field list_buf integer
---@field list_win integer
---@field columns_buf integer
---@field columns_win integer
---@field summary_buf integer
---@field summary_win integer

---Build the explore tab's three-pane layout (scratch + list panel +
---summary/columns header). Inherits buffer/window options from the
---originating source so the scratch surface matches the user's
---workspace (filetype, indent, etc.).
---@param label string
---@param initial_lines string[]
---@param source_buf integer
---@param source_win integer
---@return VF.Explore.Layout
function M.build(label, initial_lines, source_buf, source_win)
  vim.cmd("tabnew")
  local scratch_tab = v.nvim_get_current_tabpage()
  local scratch_win = v.nvim_get_current_win()
  local tabnew_buf = v.nvim_get_current_buf()

  -- undolevels = -1 disables Vim's native undo so the view is authoritative
  -- about state changes; the buffer itself stays modifiable so natural
  -- edit commands flow through unimpeded.
  local scratch_buf = buf_window.create_scratch_buffer("vimficiency://explore/" .. label, {
    undofile   = false,
    undolevels = -1,
    modifiable = true,
    modified   = false,
  })
  v.nvim_win_set_buf(scratch_win, scratch_buf)
  if v.nvim_buf_is_valid(tabnew_buf) then
    pcall(v.nvim_buf_delete, tabnew_buf, { force = true })
  end
  v.nvim_buf_set_lines(scratch_buf, 0, -1, false, initial_lines)
  buf_window.copy_buffer_options(source_buf, scratch_buf)
  buf_window.copy_window_options(source_win, scratch_win)

  -- List panel on the LEFT.
  local list_win = split_and_capture("topleft vsplit")
  local list_buf = buf_window.create_scratch_buffer(
    "vimficiency://explore/" .. label .. "/recommendations",
    { filetype = "vimficiency" })
  v.nvim_win_set_buf(list_win, list_buf)
  v.nvim_win_set_width(list_win, 44)
  util.configure_scratch_window(list_win)

  -- Header columns above the scratch.
  v.nvim_set_current_win(scratch_win)
  local columns_win = split_and_capture("aboveleft split")
  local columns_buf = buf_window.create_scratch_buffer(nil, { filetype = "vimficiency" })
  v.nvim_win_set_buf(columns_win, columns_buf)

  -- Summary header above the columns.
  v.nvim_set_current_win(columns_win)
  local summary_win = split_and_capture("aboveleft split")
  local summary_buf = buf_window.create_scratch_buffer(nil, { filetype = "vimficiency" })
  v.nvim_win_set_buf(summary_win, summary_buf)

  v.nvim_set_current_win(scratch_win)
  return {
    scratch_buf = scratch_buf, scratch_win = scratch_win, scratch_tab = scratch_tab,
    list_buf    = list_buf,    list_win    = list_win,
    columns_buf = columns_buf, columns_win = columns_win,
    summary_buf = summary_buf, summary_win = summary_win,
  }
end

return M
