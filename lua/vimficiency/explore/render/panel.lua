-- Persistent right-side settings panel for the explore view.
--
-- Same row model as `settings_modal.lua` (kind/value_kind/get/set/run
-- closures), but lives in a side-split window that stays open across
-- recommendation refreshes so the user can see the impact of each
-- adjustment in real time. Toggle via `gs` (see explore/keymaps.lua).
--
-- The panel is opened with a schema (an array of rows) and an
-- `on_change` callback. Every adjustment fires the row's `set`/`run`,
-- then `on_change`, then re-renders the panel. The panel itself owns
-- only display + keymap state — settings persistence is the caller's
-- responsibility (settings_store + settings_profile.save).
--
-- One panel per active view. Window/buffer state is stored on the view
-- as `view.panel`; the module-level functions take the view as the
-- first arg and look up state from there.
local util = require("vimficiency.util")
local highlights = require("vimficiency.highlights")
local buf_window = require("vimficiency.explore.buf_window")

local M = {}

local v = vim.api

local PANEL_WIDTH = 40

local selection_ns = v.nvim_create_namespace("vimficiency_explore_panel_selection")

-- ---------------------------------------------------------------------------
-- Row-model helpers (mirror of settings_modal's locals; intentionally
-- duplicated to keep panel self-contained. If a third consumer of this
-- shape appears, lift these into a shared module).
-- ---------------------------------------------------------------------------

local function is_selectable(row)
  return row.kind == "setting" or row.kind == "action"
end

local function format_value(row)
  if row.kind ~= "setting" then return "" end
  if row.value_kind == "bool" then return row.get() and "[✓]" or "[ ]" end
  if row.value_kind == "int" then return string.format("[%d]", row.get()) end
  if row.value_kind == "float" then
    local fmt = row.format or "[%.2f]"
    return string.format(fmt, row.get())
  end
  if row.value_kind == "enum" then return "[" .. tostring(row.get()) .. "]" end
  return "?"
end

local function adjust(row, delta)
  if row.kind == "action" then
    if delta > 0 then row.run() end
    return
  end
  if row.value_kind == "bool" then
    row.set(not row.get())
  elseif row.value_kind == "int" then
    local cur = row.get()
    local step = row.step or 1
    local next_val = cur + delta * step
    if row.min ~= nil and next_val < row.min then next_val = row.min end
    if row.max ~= nil and next_val > row.max then next_val = row.max end
    if next_val ~= cur then row.set(next_val) end
  elseif row.value_kind == "float" then
    local cur = row.get()
    local step = row.step or 0.1
    local next_val = cur + delta * step
    if row.min ~= nil and next_val < row.min then next_val = row.min end
    if row.max ~= nil and next_val > row.max then next_val = row.max end
    -- Snap to a clean number of decimal places to avoid 1.0000000004
    -- artifacts from repeated additions of e.g. 0.1.
    next_val = math.floor(next_val * 1000 + 0.5) / 1000
    if next_val ~= cur then row.set(next_val) end
  elseif row.value_kind == "enum" then
    local values = row.values
    local cur = row.get()
    local idx = 1
    for i, val in ipairs(values) do if val == cur then idx = i; break end end
    local n = #values
    local next_idx = ((idx - 1 + delta) % n + n) % n + 1
    row.set(values[next_idx])
  end
end

local function label_width(rows)
  local w = 0
  for _, row in ipairs(rows) do
    if row.kind == "setting" then w = math.max(w, #row.label) end
  end
  return w
end

---@param rows table[]
---@return { lines: string[], line_for_row: integer[] }
local function build_layout(rows)
  local label_w = label_width(rows)
  local lines = {}
  local line_for_row = {}
  for i, row in ipairs(rows) do
    line_for_row[i] = #lines + 1
    if row.kind == "separator" then
      lines[#lines + 1] = ""
    elseif row.kind == "action" then
      lines[#lines + 1] = "  " .. row.label
    elseif row.kind == "hint" then
      lines[#lines + 1] = "  " .. row.text
    else
      lines[#lines + 1] = string.format(" %-" .. label_w .. "s  %s",
        row.label, format_value(row))
    end
  end
  return { lines = lines, line_for_row = line_for_row }
end

local function first_selectable_row(rows)
  for i, row in ipairs(rows) do
    if is_selectable(row) then return i end
  end
  return nil
end

local function next_selectable_row(rows, current, step)
  local i = current + step
  while i >= 1 and i <= #rows do
    if is_selectable(rows[i]) then return i end
    i = i + step
  end
  return current
end

-- ---------------------------------------------------------------------------
-- Render
-- ---------------------------------------------------------------------------

---@param panel VF.Explore.Panel
local function render(panel)
  local layout = build_layout(panel.rows)
  vim.bo[panel.buf].modifiable = true
  v.nvim_buf_set_lines(panel.buf, 0, -1, false, layout.lines)
  vim.bo[panel.buf].modifiable = false

  v.nvim_buf_clear_namespace(panel.buf, selection_ns, 0, -1)
  local sel_line = layout.line_for_row[panel.selection]
  if sel_line then
    local line_text = assert(layout.lines[sel_line],
      "vimfy explore panel: selected row has no rendered line")
    v.nvim_buf_set_extmark(panel.buf, selection_ns, sel_line - 1, 0, {
      end_row = sel_line - 1,
      end_col = #line_text,
      hl_group = "CursorLine",
      hl_eol = true,
      priority = 50,
    })
    if v.nvim_win_is_valid(panel.win) then
      v.nvim_win_set_cursor(panel.win, { sel_line, 0 })
    end
  end
end

---@param panel VF.Explore.Panel
---@param step integer
local function move_selection(panel, step)
  panel.selection = next_selectable_row(panel.rows, panel.selection, step)
  render(panel)
end

---@param panel VF.Explore.Panel
---@param delta integer
local function adjust_selected(panel, delta)
  local row = assert(panel.rows[panel.selection],
    "vimfy explore panel: selection points past schema")
  adjust(row, delta)
  if panel.on_change then panel.on_change() end
  render(panel)
end

---Prompt the user for a direct value on the selected row. Useful for
---int/float rows with wide ranges (e.g. `maxNodesPopped` from 1k to
---500k) where stepping h/l one notch at a time is glacial. Bool/enum
---rows skip — their h/l handling is already direct.
---@param panel VF.Explore.Panel
local function prompt_selected(panel)
  local row = assert(panel.rows[panel.selection],
    "vimfy explore panel: selection points past schema")
  if row.kind ~= "setting" then return end
  if row.value_kind ~= "int" and row.value_kind ~= "float" then return end

  vim.ui.input({
    prompt = row.label .. " = ",
    default = tostring(row.get()),
  }, function(input)
    if input == nil or input == "" then return end  -- canceled
    local value = tonumber(input)
    if value == nil then
      vim.notify("vimfy panel: not a number: " .. input, vim.log.levels.WARN)
      return
    end
    if row.value_kind == "int" then value = math.floor(value) end
    if row.min ~= nil and value < row.min then value = row.min end
    if row.max ~= nil and value > row.max then value = row.max end
    row.set(value)
    if panel.on_change then panel.on_change() end
    render(panel)
  end)
end

local function install_keymaps(panel)
  local function map(lhs, fn, desc)
    vim.keymap.set("n", lhs, fn, { buffer = panel.buf, nowait = true, silent = true, desc = desc })
  end
  map("j", function() move_selection(panel, 1) end, "explore panel: next")
  map("k", function() move_selection(panel, -1) end, "explore panel: prev")
  map("<Tab>", function() adjust_selected(panel, 1) end, "explore panel: increase / next")
  map("<S-Tab>", function() adjust_selected(panel, -1) end, "explore panel: decrease / prev")
  map("<Space>", function() adjust_selected(panel, 1) end, "explore panel: toggle / activate")
  map("<CR>", function() adjust_selected(panel, 1) end, "explore panel: toggle / activate")
  map("i", function() prompt_selected(panel) end, "explore panel: input value")
  map("=", function() prompt_selected(panel) end, "explore panel: input value")
  map("q", function() M.close(panel.view) end, "explore panel: close")
  map("gs", function() M.close(panel.view) end, "explore panel: close (toggle)")
end

-- ---------------------------------------------------------------------------
-- Public API
-- ---------------------------------------------------------------------------

---@class VF.Explore.Panel
---@field buf integer
---@field win integer
---@field rows table[]
---@field selection integer
---@field on_change fun()|nil
---@field view table   # back-pointer to the active view (so keymaps can close themselves)

---Open the panel for `view`. Anchors the split to the right of the
---scratch window. Idempotent: returns the existing panel if already open.
---@param view table
---@param rows table[]    schema (same shape as settings_modal rows)
---@param on_change fun()|nil  fired after every successful adjust/run
---@return VF.Explore.Panel
function M.open(view, rows, on_change)
  if view.panel and v.nvim_win_is_valid(view.panel.win) then
    view.panel.rows = rows
    view.panel.on_change = on_change
    render(view.panel)
    return view.panel
  end

  highlights.refresh()
  v.nvim_set_current_win(view.scratch.win)
  vim.cmd("botright vsplit")
  local panel_win = v.nvim_get_current_win()
  local panel_buf = buf_window.create_scratch_buffer(
    "vimficiency://explore/panel",
    { filetype = "vimficiency" })
  v.nvim_win_set_buf(panel_win, panel_buf)
  v.nvim_win_set_width(panel_win, PANEL_WIDTH)
  util.configure_scratch_window(panel_win, { winfixwidth = true })

  local panel = {
    buf = panel_buf,
    win = panel_win,
    rows = rows,
    selection = assert(first_selectable_row(rows),
      "vimfy explore panel: schema has no selectable rows"),
    on_change = on_change,
    view = view,
  }
  view.panel = panel

  install_keymaps(panel)
  render(panel)
  return panel
end

---Close the panel. No-op if not open.
---@param view table
function M.close(view)
  local panel = view.panel
  view.panel = nil
  if not panel then return end
  if v.nvim_win_is_valid(panel.win) then
    v.nvim_win_close(panel.win, true)
  end
end

---@param view table
---@return boolean
function M.is_open(view)
  return view.panel ~= nil and v.nvim_win_is_valid(view.panel.win)
end

---Re-render after external state changes (e.g. settings reset by
---another path). No-op if the panel isn't open.
---@param view table
function M.refresh(view)
  if not M.is_open(view) then return end
  render(view.panel)
end

return M
