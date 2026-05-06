-- Persistent right-side settings panel for the explore view.
--
-- Thin presentation wrapper around `settings_window.attach`. The panel
-- owns the side-split window/buffer; the widget owns rendering, the
-- allow-list-only keymaps, cursor hiding, and the close lifecycle.
-- Toggle via `gs` (see explore/keymaps.lua).
local settings_window = require("vimficiency.settings_window")
local buf_window = require("vimficiency.explore.buf_window")

local M = {}
local v = vim.api

---@class VF.Explore.Panel
---@field buf integer
---@field win integer
---@field handle VF.SettingsWidget.Handle
---@field view table

---@param view table
---@param rows table[]
---@param on_change fun()|nil
---@return VF.Explore.Panel
function M.open(view, rows, on_change)
  if view.panel and v.nvim_win_is_valid(view.panel.win) then
    view.panel.handle.set_rows(rows)
    view.panel.on_change = on_change
    return view.panel
  end

  local panel_buf, panel_win = buf_window.create_side_pane(
    view.scratch.win,
    "right",
    "vimficiency://explore/panel",
    { filetype = "vimficiency" },
    { cursorline = false })

  local panel = {
    buf = panel_buf,
    win = panel_win,
    view = view,
  }
  view.panel = panel

  panel.handle = settings_window.attach(panel_buf, panel_win, {
    rows = rows,
    row_prefix = " ",
    value_gap = "  ",
    on_change = function()
      if on_change then on_change() end
    end,
    on_close = function() view.panel = nil end,
  })

  return panel
end

---@param view table
function M.close(view)
  if view.panel then view.panel.handle.close() end
end

---@param view table
---@return boolean
function M.is_open(view)
  return view.panel ~= nil and view.panel.handle.is_open()
end

---@param view table
function M.refresh(view)
  if view.panel then view.panel.handle.refresh() end
end

return M
