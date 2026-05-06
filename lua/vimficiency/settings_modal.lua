-- Floating-popup settings modal — same row schema and same UX as the
-- explore side panel; differs only in presentation. The shared widget
-- (`settings_window.attach`) owns rendering, keymaps, cursor hiding,
-- and the close lifecycle. This module just creates the float.
--
-- Row kinds:
--   { kind = "setting",
--     label = "Display mode",
--     value_kind = "bool" | "int" | "float" | "enum",
--     get = function() return current_value end,
--     set = function(new_value) ... end,
--     min = 0, max = 10, step = 1,        -- int/float only
--     values = { "off", "highlight" } }   -- enum only
--   { kind = "separator" }
--   { kind = "action", label = "...", run = function() ... end }
--   { kind = "hint", text = "..." }
local util = require("vimficiency.util")
local settings_window = require("vimficiency.settings_window")

local M = {}
local v = vim.api

---Open the modal. Returns a handle so callers can implement `gs` toggle.
---@param rows table[]
---@param on_change fun()|nil
---@param opts? { title?: string, on_close?: fun() }
---@return VF.SettingsWidget.Handle?
function M.open(rows, on_change, opts)
  if #rows == 0 then return nil end
  if not settings_window.first_selectable_row(rows) then return nil end
  opts = opts or {}
  local title = opts.title or "Settings"

  local row_prefix = "  "
  local value_gap = "   "
  local layout = settings_window.build_layout(rows,
    { row_prefix = row_prefix, value_gap = value_gap })

  local width = 0
  for _, line in ipairs(layout.lines) do
    width = math.max(width, vim.fn.strdisplaywidth(line))
  end
  for i, row in ipairs(rows) do
    if row.kind == "setting" then
      if row.value_kind == "enum" and row.values then
        -- Enum expansion renders unindented at col 0.
        width = math.max(width, settings_window.enum_line_width(row))
      elseif (row.value_kind == "int" or row.value_kind == "float")
          and row.min ~= nil and row.max ~= nil then
        -- Range hint renders inline after the row's value.
        local row_line = layout.lines[layout.line_for_row[i]] or ""
        width = math.max(width,
          vim.fn.strdisplaywidth(row_line)
          + vim.fn.strdisplaywidth(settings_window.format_range_hint(row)))
      end
    end
  end

  local win_width, win_height, popup_row, popup_col =
    util.centered_popup_geometry(width + 4, #layout.lines + 1)

  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency"

  local win = v.nvim_open_win(buf, true, {
    relative = "editor",
    row = popup_row,
    col = popup_col,
    width = win_width,
    height = win_height,
    style = "minimal",
    border = "rounded",
    title = " " .. title .. " ",
    title_pos = "center",
    noautocmd = true,
  })
  util.configure_scratch_window(win, { cursorline = false })

  return settings_window.attach(buf, win, {
    rows = rows,
    row_prefix = row_prefix,
    value_gap = value_gap,
    on_change = on_change,
    on_close = opts.on_close,
  })
end

return M
