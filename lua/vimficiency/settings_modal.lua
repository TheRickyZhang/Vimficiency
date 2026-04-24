-- Generic interactive settings modal.
--
-- Renders a typed row model, handles navigation and adjustment, and owns
-- no feature-specific state. Callers provide explicit rows plus any side
-- effects through get/set/run closures.
--
-- Row kinds:
--   { kind = "setting",
--     label = "Display mode",
--     value_kind = "bool" | "int" | "enum",
--     get = function() return current_value end,
--     set = function(new_value) ... end,
--     min = 0, max = 10,                  -- int only
--     values = { "off", "highlight" } }  -- enum only
--
--   { kind = "separator" }
--
--   { kind = "action",
--     label = "reset to default settings",
--     run = function() ... end }
--
--   { kind = "hint",
--     text = "..." }
local util = require("vimficiency.util")
local highlights = require("vimficiency.highlights")

local M = {}

local v = vim.api

-- Namespace for the enum-expansion virt-line that appears under the
-- selected row. Dedicated so we can clear it independently of the
-- buffer's line content.
local expansion_ns = v.nvim_create_namespace("vimficiency_settings_expansion")
local selection_ns = v.nvim_create_namespace("vimficiency_settings_selection")

---@param row table
---@return boolean
local function is_selectable(row)
  return row.kind == "setting" or row.kind == "action"
end

---Apply `delta ∈ {+1, -1}` to the given row, mutating via its `set`
---(or firing `run` for action rows on forward adjust only).
---@param row table
---@param delta integer
local function adjust(row, delta)
  if row.kind == "action" then
    if delta > 0 then row.run() end
    return
  end
  if row.value_kind == "bool" then
    row.set(not row.get())
  elseif row.value_kind == "int" then
    local cur = row.get()
    local next_val = cur + delta
    if row.min ~= nil and next_val < row.min then next_val = row.min end
    if row.max ~= nil and next_val > row.max then next_val = row.max end
    if next_val ~= cur then row.set(next_val) end
  elseif row.value_kind == "enum" then
    local values = row.values
    local cur = row.get()
    local idx = 1
    for i, v_ in ipairs(values) do if v_ == cur then idx = i; break end end
    local n = #values
    local next_idx = ((idx - 1 + delta) % n + n) % n + 1
    row.set(values[next_idx])
  end
end

---@param row table
---@return string
local function format_value(row)
  if row.value_kind == "bool" then
    return row.get() and "[✓]" or "[ ]"
  end
  if row.value_kind == "int" then
    return string.format("[%d]", row.get())
  end
  if row.value_kind == "enum" then
    return "[" .. tostring(row.get()) .. "]"
  end
  return "?"
end

---@param rows table[]
---@return integer
local function label_width(rows)
  local w = 0
  for _, row in ipairs(rows) do
    if row.kind == "setting" then
      w = math.max(w, #row.label)
    end
  end
  return w
end

---@param rows table[]
---@return { lines: string[], line_for_row: integer[], label_w: integer, value_col: integer }
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
      lines[#lines + 1] = string.format("  %-" .. label_w .. "s   %s",
        row.label, format_value(row))
    end
  end

  return {
    lines = lines,
    line_for_row = line_for_row,
    label_w = label_w,
    value_col = label_w + 5,
  }
end

---@param rows table[]
---@return integer?
local function first_selectable_row(rows)
  for i, row in ipairs(rows) do
    if is_selectable(row) then return i end
  end
  return nil
end

---@param rows table[]
---@param current integer
---@param step integer
---@return integer
local function next_selectable_row(rows, current, step)
  local i = current + step
  while i >= 1 and i <= #rows do
    if is_selectable(rows[i]) then return i end
    i = i + step
  end
  return current
end

---@param rows table[]
---@param on_change fun()|nil
---@param opts? { title?: string, on_close?: fun() }
function M.open(rows, on_change, opts)
  if #rows == 0 then return end
  local selection = first_selectable_row(rows)
  if not selection then return end
  opts = opts or {}
  local title = opts.title or "Settings"
  local on_close = opts.on_close

  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency"
  highlights.refresh()

  local layout

  local function render()
    layout = build_layout(rows)
    vim.bo[buf].modifiable = true
    v.nvim_buf_set_lines(buf, 0, -1, false, layout.lines)
    vim.bo[buf].modifiable = false
  end

  local function clamp_selection()
    if selection < 1 then selection = 1 end
    if selection > #rows then selection = #rows end
    if not is_selectable(rows[selection]) then
      local next_up = next_selectable_row(rows, selection, -1)
      local next_down = next_selectable_row(rows, selection, 1)
      selection = next_up ~= selection and next_up or next_down
    end
  end

  local function render_selection()
    v.nvim_buf_clear_namespace(buf, selection_ns, 0, -1)
    clamp_selection()
    local row = rows[selection]
    if not row or row.kind ~= "action" then return end
    local line_idx = layout.line_for_row[selection] - 1
    local line = layout.lines[layout.line_for_row[selection]] or ""
    v.nvim_buf_set_extmark(buf, selection_ns, line_idx, 0, {
      end_row = line_idx,
      end_col = #line,
      hl_group = highlights.SETTINGS_ACTION_ACTIVE,
      priority = 2100,
    })
  end

  local function render_expansion(win)
    v.nvim_buf_clear_namespace(buf, expansion_ns, 0, -1)
    clamp_selection()
    local row = rows[selection]
    if not row or row.kind ~= "setting" then return end

    local line_idx = layout.line_for_row[selection] - 1
    local indent = string.rep(" ", layout.value_col)
    local virt_lines = {}

    if row.value_kind == "enum" and row.values then
      local current = row.get()
      local max_width = math.max(8, v.nvim_win_get_width(win) - layout.value_col - 2)
      local chunks = { { indent, "Normal" } }
      local used = 0

      for i, option in ipairs(row.values) do
        local sep = (i > 1 and used > 0) and "  " or ""
        local option_width = vim.fn.strdisplaywidth(option)
        if used > 0 and used + #sep + option_width > max_width then
          virt_lines[#virt_lines + 1] = chunks
          chunks = { { indent, "Normal" } }
          used = 0
          sep = ""
        end
        if sep ~= "" then
          chunks[#chunks + 1] = { sep, "Normal" }
          used = used + #sep
        end
        local hl = (option == current) and "Title" or "Comment"
        chunks[#chunks + 1] = { option, hl }
        used = used + option_width
      end
      virt_lines[#virt_lines + 1] = chunks
    elseif row.value_kind == "int" and row.min ~= nil and row.max ~= nil then
      virt_lines[1] = {
        { indent, "Normal" },
        { string.format("%d..%d", row.min, row.max), "Comment" },
      }
    else
      return
    end

    v.nvim_buf_set_extmark(buf, expansion_ns, line_idx, 0, {
      virt_lines = virt_lines,
      virt_lines_above = false,
    })
  end

  local function place_cursor(win)
    clamp_selection()
    pcall(v.nvim_win_set_cursor, win, { layout.line_for_row[selection], 2 })
    render_selection()
    render_expansion(win)
  end

  render()

  local lines = layout.lines
  local width = 0
  for _, line in ipairs(lines) do
    width = math.max(width, vim.fn.strdisplaywidth(line))
  end
  for _, row in ipairs(rows) do
    if row.kind == "setting" and row.value_kind == "enum" and row.values then
      local w = layout.value_col
      for i, option in ipairs(row.values) do
        if i > 1 then w = w + 2 end
        w = w + vim.fn.strdisplaywidth(option)
      end
      width = math.max(width, w)
    end
  end

  local win_width, win_height, row, col =
    util.centered_popup_geometry(width + 4, #lines + 1)

  local win = v.nvim_open_win(buf, true, {
    relative = "editor",
    row = row,
    col = col,
    width = win_width,
    height = win_height,
    style = "minimal",
    border = "rounded",
    title = " " .. title .. " ",
    title_pos = "center",
    noautocmd = true,
  })
  util.configure_scratch_window(win, { cursorline = true })
  place_cursor(win)

  local function close_popup()
    if v.nvim_win_is_valid(win) then v.nvim_win_close(win, true) end
    if on_close then on_close() end
  end

  local function move(step)
    selection = next_selectable_row(rows, selection, step)
    place_cursor(win)
  end

  local function adjust_selection(delta)
    clamp_selection()
    adjust(rows[selection], delta)
    render()
    place_cursor(win)
    if on_change then on_change() end
  end

  util.set_buffer_keymaps(buf, util.with_standard_ui_keymaps({
    { lhs = "j",       handler = function() move(1) end,             desc = "Next setting",
      summary_group = "move_next", summary_desc = "Next setting" },
    { lhs = "<Down>",  handler = function() move(1) end,             desc = "Next setting",
      summary_group = "move_next", summary_desc = "Next setting" },
    { lhs = "k",       handler = function() move(-1) end,            desc = "Previous setting",
      summary_group = "move_prev", summary_desc = "Previous setting" },
    { lhs = "<Up>",    handler = function() move(-1) end,            desc = "Previous setting",
      summary_group = "move_prev", summary_desc = "Previous setting" },

    { lhs = "<Space>", handler = function() adjust_selection(1) end, desc = "Forward-adjust",
      summary_group = "adjust_forward", summary_desc = "Forward-adjust" },
    { lhs = "<CR>",    handler = function() adjust_selection(1) end, desc = "Forward-adjust",
      summary_group = "adjust_forward", summary_desc = "Forward-adjust" },
    { lhs = "<Tab>",   handler = function() adjust_selection(1) end, desc = "Forward-adjust",
      summary_group = "adjust_forward", summary_desc = "Forward-adjust" },
    { lhs = "<Right>", handler = function() adjust_selection(1) end, desc = "Forward-adjust",
      summary_group = "adjust_forward", summary_desc = "Forward-adjust" },
    { lhs = "l",       handler = function() adjust_selection(1) end, desc = "Forward-adjust",
      summary_group = "adjust_forward", summary_desc = "Forward-adjust" },
    { lhs = "+",       handler = function() adjust_selection(1) end, desc = "Forward-adjust",
      summary_group = "adjust_forward", summary_desc = "Forward-adjust" },

    { lhs = "<S-Tab>", handler = function() adjust_selection(-1) end, desc = "Backward-adjust",
      summary_group = "adjust_backward", summary_desc = "Backward-adjust" },
    { lhs = "<Left>",  handler = function() adjust_selection(-1) end, desc = "Backward-adjust",
      summary_group = "adjust_backward", summary_desc = "Backward-adjust" },
    { lhs = "h",       handler = function() adjust_selection(-1) end, desc = "Backward-adjust",
      summary_group = "adjust_backward", summary_desc = "Backward-adjust" },
    { lhs = "-",       handler = function() adjust_selection(-1) end, desc = "Backward-adjust",
      summary_group = "adjust_backward", summary_desc = "Backward-adjust" },

    { lhs = "q",       handler = close_popup,                        desc = "Close settings",
      summary_group = "close", summary_desc = "Close settings" },
    { lhs = "<Esc>",   handler = close_popup,                        desc = "Close settings",
      summary_group = "close", summary_desc = "Close settings" },
  }, {
    title = title .. " Keys",
    docs = true,
  }))
end

return M
