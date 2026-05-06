local v          = vim.api
local buf_window = require("vimficiency.explore.buf_window")

local M = {}

---@class VF.Explore.Layout
---@field scratch_buf integer
---@field scratch_win integer
---@field scratch_tab integer
---@field list_buf integer
---@field list_win integer

---Build the explore tab's two-pane layout (scratch + list panel).
---Header is rendered as virt_lines extmarks on the scratch buffer
---(see render/header.lua), so no separate header window is needed.
---Scratch inherits buffer/window options from the originating source so
---it matches the user's workspace (filetype, indent, etc.).
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

  local list_buf, list_win = buf_window.create_side_pane(
    scratch_win,
    "left",
    "vimficiency://explore/" .. label .. "/recommendations",
    { filetype = "vimficiency" })

  v.nvim_set_current_win(scratch_win)
  return {
    scratch_buf = scratch_buf, scratch_win = scratch_win, scratch_tab = scratch_tab,
    list_buf    = list_buf,    list_win    = list_win,
  }
end

return M
