-- Full-screen viewer: header + side-by-side initial/final panes. Changed
-- columns come from the optimizer's own diff (result.diffs); see session.view.

local v            = vim.api
local buf_window   = require("vimficiency.explore.buf_window")
local util         = require("vimficiency.util")
local highlights   = require("vimficiency.highlights")
local result_view  = require("vimficiency.session.result_view")
local ffi_display  = require("vimficiency.ffi.display")
local ffi_optimizer = require("vimficiency.ffi.optimizer")

local M = {}

local ns = v.nvim_create_namespace("vimfy_result_view")

local MAX_HEADER_RECS = 3

---@param c number|nil
---@return string
local function fmt_cost(c)
  return c and string.format("%.0f", c) or "?"
end

---@param result VF.Session.Result
---@return string[]
local function build_header_lines(result)
  local lines = { result_view.format_position(result) .. result_view.format_reason_suffix(result) }
  lines[#lines + 1] = string.format("you   %s   (%s)",
    ffi_display.sequence_display_inline(result.user_seq or ""), fmt_cost(result.user_cost))
  local recs = result.optimal_results or {}
  for i = 1, math.min(MAX_HEADER_RECS, #recs) do
    lines[#lines + 1] = string.format("%d.    %s   (%s)",
      i, ffi_display.sequence_display_inline(recs[i].seq or ""), fmt_cost(recs[i].cost))
  end
  if result.had_mouse then
    lines[#lines + 1] = result_view.MOUSE_WARNING
  end
  return lines
end

---@param cmd string
---@return integer win  The window the split created.
local function split_capture(cmd)
  local tab = v.nvim_get_current_tabpage()
  local before = {}
  for _, w in ipairs(v.nvim_tabpage_list_wins(tab)) do before[w] = true end
  vim.cmd(cmd)
  for _, w in ipairs(v.nvim_tabpage_list_wins(tab)) do
    if not before[w] then return w end
  end
  error("vimfy view: split '" .. cmd .. "' created no window")
end

---@param name string
---@param lines string[]
---@return integer buf
local function make_pane_buffer(name, lines)
  local buf = buf_window.create_scratch_buffer(name, { modifiable = true })
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].modifiable = false
  return buf
end

--- Highlight one cursor cell, mirroring simulate's normal-mode cursor rule.
---@param buf integer
---@param lines string[]
---@param row integer
---@param col integer
---@param hl string
local function highlight_cursor(buf, lines, row, col, hl)
  local line = lines[row + 1] or ""
  if #line == 0 then
    v.nvim_buf_set_extmark(buf, ns, row, 0, {
      virt_text = { { " ", hl } }, virt_text_pos = "overlay", priority = 250 })
  elseif col < #line then
    v.nvim_buf_set_extmark(buf, ns, row, col, { end_col = col + 1, hl_group = hl, priority = 250 })
  else
    v.nvim_buf_set_extmark(buf, ns, row, #line - 1, { end_col = #line, hl_group = hl, priority = 250 })
  end
end

--- Highlight the changed columns on one side. `endPos` may be a virtual column
--- (DiffState half-open semantics), so clamp end_col to the line length.
---@param buf integer
---@param lines string[]
---@param regions VF.Diff.Region[]
---@param side "init"|"goal"
---@param hl string
local function highlight_diffs(buf, lines, regions, side, hl)
  for _, r in ipairs(regions) do
    local s = r[side]
    local empty = s.begin_row == s.end_row and s.begin_col == s.end_col
    if not empty then
      local end_line = lines[s.end_row + 1] or ""
      v.nvim_buf_set_extmark(buf, ns, s.begin_row, s.begin_col, {
        end_row = s.end_row,
        end_col = math.min(s.end_col, #end_line),
        hl_group = hl,
        priority = 200,
      })
    end
  end
end

--- Faded out-of-slice markers and reserved boundary prefix/suffix.
---@param buf integer
---@param lines string[]
---@param result VF.Session.Result
local function highlight_context(buf, lines, result)
  local dim = highlights.VIEW_CONTEXT_DIM
  local last = #lines - 1
  if result.has_lines_above then
    v.nvim_buf_set_extmark(buf, ns, 0, 0, {
      virt_lines = { { { "(… lines above …)", dim } } }, virt_lines_above = true })
  end
  if result.has_lines_below then
    v.nvim_buf_set_extmark(buf, ns, last, 0, {
      virt_lines = { { { "(… lines below …)", dim } } } })
  end
  -- Reserved: whole-buffer slices carry empty prefix/suffix (see compute.lua);
  -- nonempty values only arise for future per-planned-edit views.
  if result.prefix and result.prefix ~= "" then
    v.nvim_buf_set_extmark(buf, ns, 0, 0, {
      virt_text = { { result.prefix, dim } }, virt_text_pos = "inline" })
  end
  if result.suffix and result.suffix ~= "" then
    v.nvim_buf_set_extmark(buf, ns, last, #(lines[last + 1] or ""), {
      virt_text = { { result.suffix, dim } }, virt_text_pos = "inline" })
  end
end

---@param win integer
---@param row integer
---@param col integer
local function place_cursor(win, lines, row, col)
  local clamped_row = math.max(0, math.min(row, #lines - 1))
  local line = lines[clamped_row + 1] or ""
  v.nvim_win_set_cursor(win, { clamped_row + 1, math.max(0, math.min(col, #line)) })
end

--- Open the viewer for a resolved result under `label`.
---@param label string
---@param result VF.Session.Result
function M.open(label, result)
  highlights.refresh()

  local initial_lines = result.lines
  local goal_lines = result.goal_lines or result.lines
  local diffs = result.diffs
  if diffs == nil then
    diffs = ffi_optimizer.compute_diffs(initial_lines, goal_lines)
  end

  local header_lines = build_header_lines(result)

  local saved_tab = v.nvim_get_current_tabpage()
  local saved_win = v.nvim_get_current_win()

  vim.cmd("tabnew")
  local view_tab = v.nvim_get_current_tabpage()
  local left_win = v.nvim_get_current_win()
  local placeholder_buf = v.nvim_get_current_buf()

  -- Header full-width on top; the original tab window becomes the left pane,
  -- which we then vsplit into the right pane.
  v.nvim_set_current_win(left_win)
  local header_win = split_capture("aboveleft split")
  v.nvim_set_current_win(left_win)
  local right_win = split_capture("rightbelow vsplit")

  local header_buf = make_pane_buffer("vimficiency://view/" .. label .. "/header", header_lines)
  local left_buf = make_pane_buffer("vimficiency://view/" .. label .. "/initial", initial_lines)
  local right_buf = make_pane_buffer("vimficiency://view/" .. label .. "/final", goal_lines)

  v.nvim_win_set_buf(header_win, header_buf)
  v.nvim_win_set_buf(left_win, left_buf)
  v.nvim_win_set_buf(right_win, right_buf)
  if v.nvim_buf_is_valid(placeholder_buf) then
    pcall(v.nvim_buf_delete, placeholder_buf, { force = true })
  end

  util.configure_scratch_window(header_win, { winfixheight = true })
  util.configure_scratch_window(left_win, { cursorline = true })
  util.configure_scratch_window(right_win, { cursorline = true })
  v.nvim_win_set_height(header_win, math.max(1, #header_lines))

  highlight_diffs(left_buf, initial_lines, diffs, "init", highlights.VIEW_DIFF_DELETE)
  highlight_diffs(right_buf, goal_lines, diffs, "goal", highlights.VIEW_DIFF_ADD)
  highlight_cursor(left_buf, initial_lines, result.start_row, result.start_col, highlights.VIEW_CURSOR_START)
  highlight_cursor(right_buf, goal_lines, result.end_row, result.end_col, highlights.VIEW_CURSOR_END)
  highlight_context(left_buf, initial_lines, result)
  highlight_context(right_buf, goal_lines, result)

  place_cursor(left_win, initial_lines, result.start_row, result.start_col)
  place_cursor(right_win, goal_lines, result.end_row, result.end_col)
  v.nvim_set_current_win(left_win)

  local function close()
    if v.nvim_tabpage_is_valid(view_tab) then
      if v.nvim_tabpage_is_valid(saved_tab) then
        v.nvim_set_current_tabpage(saved_tab)
      end
      pcall(vim.cmd, "tabclose " .. v.nvim_tabpage_get_number(view_tab))
    end
    if v.nvim_win_is_valid(saved_win) then
      v.nvim_set_current_win(saved_win)
    end
  end

  local keymaps = util.with_standard_ui_keymaps({
    { lhs = "q", handler = close, desc = "Close vimfy view", nowait = true },
  }, { title = "Vimfy View Keys", docs = true })
  util.set_buffer_keymaps(header_buf, keymaps)
  util.set_buffer_keymaps(left_buf, keymaps)
  util.set_buffer_keymaps(right_buf, keymaps)
end

return M
