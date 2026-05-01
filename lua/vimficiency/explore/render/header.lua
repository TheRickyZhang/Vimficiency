-- Fixed header panes above the explore scratch buffer.
local v = vim.api
local sequence_display = require("vimficiency.sequence_display")
local util = require("vimficiency.util")

local M = {}

local header_ns = v.nvim_create_namespace("vimfy_explore_header")
M.header_ns = header_ns

---@param active VF.Explore.Active
---@param remaining string
---@return table
local function summary_chunks(active, remaining)
  local phase = active.state.phase
  local phase_label = phase.kind
  if phase.kind == "Navigate" and phase.edit_index ~= nil then
    phase_label = string.format("Navigate %d/%d",
      phase.edit_index + 1, math.max(active.state.total_edits, 1))
  elseif phase.kind == "Transform" then
    phase_label = string.format("Transform %d/%d",
      phase.edit_index + 1, math.max(active.state.total_edits, 1))
  elseif phase.kind == "Insert" then
    phase_label = string.format("Insert '%s'",
      sequence_display.inline(remaining, { sectionize = false }))
  end
  return {
    { "Cost ", "Comment" },
    { string.format("%.2f", active.state.accepted_cost), "Normal" },
    { "   Cursor ", "Comment" },
    { string.format("(%d,%d)", active.state.cursor.row, active.state.cursor.col), "Normal" },
    { "   Phase ", "Comment" },
    { phase_label, "Normal" },
  }
end

---@param active VF.Explore.Active
---@return { title: string, seq: string, empty_text: string }[]
local function gather_columns(active)
  local cols = {
    { title = "Explored", seq = active.state.accepted_seq or "", empty_text = "(start)" },
  }
  local optimal = (active.result and active.result.optimal_results) or {}
  local want = math.min(active.show_result_count or 0, #optimal)
  for i = 1, want do
    cols[#cols + 1] = {
      title = string.format("Optimal %d", i),
      seq = optimal[i].seq or "",
      empty_text = "(none)",
    }
  end
  if active.show_user_typed then
    cols[#cols + 1] = {
      title = "User typed",
      seq = (active.result and active.result.user_seq) or "",
      empty_text = "(none)",
    }
  end
  return cols
end

---@param active VF.Explore.Active
---@param pane VF.Explore.Window
---@param name string
local function configure_window(active, pane, name)
  v.nvim_buf_set_name(pane.buf, name)
  vim.bo[pane.buf].buftype = "nofile"
  vim.bo[pane.buf].bufhidden = "wipe"
  vim.bo[pane.buf].swapfile = false
  vim.bo[pane.buf].modifiable = false
  vim.bo[pane.buf].filetype = "vimficiency"
  util.configure_scratch_window(pane.win, { winfixheight = true })
  if active.header_handlers then
    require("vimficiency.explore.keymaps").install_header(pane.buf, active.header_handlers)
  end
end

local function sort_windows_left_to_right(windows)
  table.sort(windows, function(a, b)
    local pa = v.nvim_win_get_position(a.win)
    local pb = v.nvim_win_get_position(b.win)
    if pa[1] ~= pb[1] then return pa[1] < pb[1] end
    return pa[2] < pb[2]
  end)
end

local chunk_text

---@param active VF.Explore.Active
---@param pane VF.Explore.Window
---@param idx integer
local function configure_pane(active, pane, idx)
  configure_window(active, pane, string.format("vimficiency://explore/%s/header/%d", active.label, idx))
end

---@param active VF.Explore.Active
---@param source_win integer
---@return VF.Explore.Window
local function split_new_pane(active, source_win)
  local before = v.nvim_tabpage_list_wins(active.scratch.tab)
  v.nvim_set_current_win(source_win)
  vim.cmd("rightbelow vsplit")
  local new_win
  for _, win in ipairs(v.nvim_tabpage_list_wins(active.scratch.tab)) do
    local seen = false
    for _, prev in ipairs(before) do
      if prev == win then
        seen = true
        break
      end
    end
    if not seen then
      new_win = win
      break
    end
  end
  assert(new_win, "vimfy explore: failed to create header pane")
  local buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(new_win, buf)
  return { buf = buf, win = new_win }
end

---@param active VF.Explore.Active
---@param count integer
local function ensure_panes(active, count)
  local current_win = v.nvim_get_current_win()
  local valid = {}
  for _, pane in ipairs(active.header.windows or {}) do
    if v.nvim_win_is_valid(pane.win) and v.nvim_buf_is_valid(pane.buf) then
      valid[#valid + 1] = pane
    end
  end
  active.header.windows = valid
  if #active.header.windows == 0 then return end

  active.header.rebuilding = true
  if active.header.summary and v.nvim_win_is_valid(active.header.summary.win)
      and v.nvim_buf_is_valid(active.header.summary.buf) then
    configure_window(active, active.header.summary,
      string.format("vimficiency://explore/%s/header/summary", active.label))
  end

  while #active.header.windows < count do
    local pane = split_new_pane(active, active.header.windows[#active.header.windows].win)
    active.header.windows[#active.header.windows + 1] = pane
  end

  while #active.header.windows > count do
    local pane = table.remove(active.header.windows)
    if v.nvim_win_is_valid(pane.win) then
      v.nvim_win_close(pane.win, true)
    end
    if v.nvim_buf_is_valid(pane.buf) then
      pcall(v.nvim_buf_delete, pane.buf, { force = true })
    end
  end

  sort_windows_left_to_right(active.header.windows)
  for i, pane in ipairs(active.header.windows) do
    configure_pane(active, pane, i)
  end

  active.header.rebuilding = false
  if v.nvim_win_is_valid(current_win) then
    v.nvim_set_current_win(current_win)
  elseif v.nvim_win_is_valid(active.scratch.win) then
    v.nvim_set_current_win(active.scratch.win)
  end
end

---@param rows table[][]
---@return integer
local function rows_display_width(rows)
  local width = 0
  for _, row in ipairs(rows) do
    width = math.max(width, vim.fn.strdisplaywidth(chunk_text(row)))
  end
  return width
end

---@param active VF.Explore.Active
---@param pane_rows table[][]
local function set_compact_widths(active, pane_rows)
  local count = #active.header.windows
  if count == 0 then return end

  local total_width = 0
  for _, pane in ipairs(active.header.windows) do
    total_width = total_width + v.nvim_win_get_width(pane.win)
  end

  local min_width = 12
  local desired = {}
  local desired_total = 0
  for i = 1, count do
    desired[i] = math.max(min_width, rows_display_width(pane_rows[i]) + 1)
    desired_total = desired_total + desired[i]
  end

  if desired_total <= total_width then
    local remaining = total_width
    for i, pane in ipairs(active.header.windows) do
      local target = (i == count) and remaining or desired[i]
      remaining = remaining - target
      pcall(v.nvim_win_set_width, pane.win, math.max(min_width, target))
    end
    return
  end

  local scaled = {}
  local scaled_total = 0
  for i = 1, count do
    scaled[i] = math.max(min_width,
      math.floor(desired[i] * total_width / math.max(desired_total, 1)))
    scaled_total = scaled_total + scaled[i]
  end

  if scaled_total > total_width then
    local overflow = scaled_total - total_width
    for i = count, 1, -1 do
      local shrink = math.min(overflow, scaled[i] - min_width)
      scaled[i] = scaled[i] - shrink
      overflow = overflow - shrink
      if overflow == 0 then break end
    end
  end

  local remaining = total_width
  for i, pane in ipairs(active.header.windows) do
    local target = (i == count) and remaining or scaled[i]
    remaining = remaining - target
    pcall(v.nvim_win_set_width, pane.win, math.max(min_width, target))
  end
end

---@param chunks table[]
---@return string
chunk_text = function(chunks)
  local parts = {}
  for _, chunk in ipairs(chunks) do
    parts[#parts + 1] = chunk[1]
  end
  return table.concat(parts)
end

---@param buf integer
---@param rows table[][]
local function write_rows(buf, rows)
  local lines = {}
  for i, row in ipairs(rows) do
    lines[i] = chunk_text(row)
  end

  vim.bo[buf].modifiable = true
  v.nvim_buf_clear_namespace(buf, header_ns, 0, -1)
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)

  for row_idx, row in ipairs(rows) do
    local col = 0
    for _, chunk in ipairs(row) do
      local text = chunk[1]
      local hl = chunk[2]
      local next_col = col + #text
      if text ~= "" and hl and hl ~= "Normal" then
        v.nvim_buf_set_extmark(buf, header_ns, row_idx - 1, col, {
          end_row = row_idx - 1,
          end_col = next_col,
          hl_group = hl,
          priority = 2200,
        })
      end
      col = next_col
    end
  end
  vim.bo[buf].modifiable = false
end

---@param column { title: string, seq: string, empty_text: string }
---@return table[][]
local function build_rows(column)
  local lines
  if column.seq ~= "" then
    lines = sequence_display.lines(column.seq)
  else
    lines = { column.empty_text }
  end

  local rows = {
    { { column.title, "Title" } },
    { { "", "Normal" } },
  }
  for _, line in ipairs(lines) do
    rows[#rows + 1] = { { line, "Normal" } }
  end
  rows[#rows + 1] = { { "", "Normal" } }
  return rows
end

---@param active VF.Explore.Active
---@param remaining string
---@return table[][]
local function build_summary_rows(active, remaining)
  return {
    { { "", "Normal" } },
    { { "Explore", "Title" }, { " " .. active.label, "Comment" } },
    summary_chunks(active, remaining),
    { { "", "Normal" } },
  }
end

---@param rows table[][]
---@param target integer
local function pad_rows(rows, target)
  while #rows < target do
    rows[#rows + 1] = { { "", "Normal" } }
  end
end

---@param active VF.Explore.Active
---@param remaining string
---@return nil
function M.render(active, remaining)
  if not (active.header and active.header.summary and active.header.windows and #active.header.windows > 0) then
    return
  end

  local columns = gather_columns(active)
  ensure_panes(active, #columns)
  if #active.header.windows ~= #columns then
    return
  end

  local pane_rows = {}
  local max_height = 0
  local summary_rows = build_summary_rows(active, remaining)
  for i, column in ipairs(columns) do
    pane_rows[i] = build_rows(column)
    max_height = math.max(max_height, #pane_rows[i])
  end
  set_compact_widths(active, pane_rows)

  if not (v.nvim_win_is_valid(active.header.summary.win) and v.nvim_buf_is_valid(active.header.summary.buf)) then
    return
  end
  write_rows(active.header.summary.buf, summary_rows)
  pcall(v.nvim_win_set_height, active.header.summary.win, #summary_rows)

  for i, pane in ipairs(active.header.windows) do
    if not (v.nvim_win_is_valid(pane.win) and v.nvim_buf_is_valid(pane.buf)) then
      return
    end
    pad_rows(pane_rows[i], max_height)
    write_rows(pane.buf, pane_rows[i])
    pcall(v.nvim_win_set_height, pane.win, max_height)
  end
end

return M
