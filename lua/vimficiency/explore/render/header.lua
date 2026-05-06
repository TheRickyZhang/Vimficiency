-- Header decoration on the explore scratch buffer. Renders as virt_lines
-- extmarks pinned to the topline so the columns behave like uninteractable
-- virtual rows: no focus model to fight, no width redistribution, no
-- separate windows or buffers.
local v = vim.api
local chunks_util = require("vimficiency.chunks")
local insert_helpers = require("vimficiency.explore.insert_helpers")
local sequence_display = require("vimficiency.sequence_display")

local M = {}

local header_ns = v.nvim_create_namespace("vimfy_explore_header")
M.header_ns = header_ns

local MIN_COLUMN_WIDTH = 12
local MAX_COLUMN_WIDTH = 32
local COLUMN_GAP = 1

---@param active VF.Explore.Active
---@param continuation VF.Explore.InsertContinuation
---@return table[]
local function summary_chunks(active, continuation)
  local phase = active.state.phase
  local phase_label = phase.kind
  local label_hl = "Normal"
  if active.warning then
    phase_label = "WARNING - invariant check failed"
    label_hl = "DiagnosticError"
  elseif active.state.is_completed then
    -- Sticky "Done" supersedes phase display; the actual phase
    -- (Navigate at totalEdits) leaks an off-by-one "N+1/N" otherwise.
    phase_label = "Done — u to undo, q to close"
    label_hl = "DiagnosticOk"
  elseif phase.kind == "Navigate" and phase.edit_index ~= nil then
    phase_label = string.format("Navigate %d/%d",
      phase.edit_index + 1, math.max(active.state.total_edits, 1))
  elseif phase.kind == "Transform" then
    phase_label = string.format("Transform %d/%d",
      phase.edit_index + 1, math.max(active.state.total_edits, 1))
  elseif phase.kind == "Insert" then
    phase_label = string.format("Insert '%s'",
      sequence_display.typed_chunks_inline(continuation.chunks))
  end
  return {
    { "Cost ", "Comment" },
    { string.format("%.2f", active.state.cost), "Normal" },
    { "   Cursor ", "Comment" },
    { string.format("(%d,%d)", active.state.cursor.row, active.state.cursor.col), "Normal" },
    { "   Phase ", "Comment" },
    { phase_label, label_hl },
  }
end

---A column may carry plan-aligned `rows` (already-segmented strings, one per
---visual line) OR a raw `seq` to be tokenized in Lua. Explored and Optimal-N
---use `rows` so Vim-token-fused edits like `dE` and `ce i+1<Esc>` render as
---the planned-edit boundaries the optimizer actually chose. User typed has
---no plan boundaries — keep it on the legacy seq path so the UI doesn't
---fabricate structure.
---@param active VF.Explore.Active
---@return { title: string, rows?: string[], seq?: string, live_seq?: string, live_typed?: string, empty_text: string }[]
local function gather_columns(active)
  local header_rows = active.header_rows
  local live_seq = ""
  local live_typed = ""
  if active.state.phase.kind == "Insert" then
    live_seq = active.on_key_buffer or ""
    if live_seq == "" then
      live_typed = insert_helpers.current_typed(active)
    end
  end
  local cols = {
    {
      title = "Explored",
      rows = header_rows.explored,
      live_seq = live_seq,
      live_typed = live_typed,
      empty_text = "(start)",
    },
  }
  if active.show_user_typed then
    cols[#cols + 1] = {
      title = "User typed",
      seq = active.result.user_seq,
      empty_text = "(none)",
    }
  end
  local want = math.min(active.show_result_count, #header_rows.optimal)
  for i = 1, want do
    cols[#cols + 1] = {
      title = string.format("Optimal %d", i),
      rows = header_rows.optimal[i],
      empty_text = "(none)",
    }
  end
  return cols
end

---Build the column's display lines as a list of plain strings (one per
---visual row in the column).
---@param column { title: string, rows?: string[], seq?: string, live_seq?: string, live_typed?: string, empty_text: string }
---@return string[]
local function column_content_lines(column)
  local lines
  if column.rows then
    if #column.rows == 0 then
      lines = { column.empty_text }
    else
      lines = {}
      for i, raw in ipairs(column.rows) do
        lines[i] = sequence_display.inline(raw)
      end
    end
  else
    local seq = assert(column.seq, "vimfy explore: header column missing sequence")
    if seq ~= "" then
      lines = sequence_display.lines(seq)
    else
      lines = { column.empty_text }
    end
  end
  if column.live_seq and column.live_seq ~= "" then
    lines[#lines + 1] = sequence_display.inline(column.live_seq)
  elseif column.live_typed and column.live_typed ~= "" then
    lines[#lines + 1] = sequence_display.literal_typed_text_inline(column.live_typed)
  end
  return lines
end

---@param column { title: string, rows?: string[], seq?: string, live_seq?: string, live_typed?: string, empty_text: string }
---@return table[][]
local function build_column_rows(column)
  local lines = column_content_lines(column)
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

---@param rows table[][]
---@return integer
local function rows_max_display_width(rows)
  local w = 0
  for _, row in ipairs(rows) do
    w = math.max(w, chunks_util.display_width(row))
  end
  return w
end

---Fit desired widths exactly when there's room, otherwise scale down
---proportionally with a min-width floor. Each column is capped at
---MAX_COLUMN_WIDTH; over-cap content wraps (see `wrap_column_rows`).
---@param rows_per_column table[][][]
---@param available_width integer
---@return integer[]
local function compute_column_widths(rows_per_column, available_width)
  local count = #rows_per_column
  local desired = {}
  local desired_total = 0
  for i = 1, count do
    local content = rows_max_display_width(rows_per_column[i])
    desired[i] = math.max(MIN_COLUMN_WIDTH, math.min(MAX_COLUMN_WIDTH, content))
    desired_total = desired_total + desired[i]
  end

  local total_gap = math.max(0, count - 1) * COLUMN_GAP
  local budget = math.max(count * MIN_COLUMN_WIDTH, available_width - total_gap)

  if desired_total <= budget then
    return desired
  end

  local widths = {}
  local total = 0
  for i = 1, count do
    widths[i] = math.max(MIN_COLUMN_WIDTH,
      math.floor(desired[i] * budget / math.max(desired_total, 1)))
    total = total + widths[i]
  end
  if total > budget then
    local overflow = total - budget
    for i = count, 1, -1 do
      local shrink = math.min(overflow, widths[i] - MIN_COLUMN_WIDTH)
      widths[i] = widths[i] - shrink
      overflow = overflow - shrink
      if overflow == 0 then break end
    end
  end
  return widths
end

---Replace any over-width row in `rows` with the multi-line wrap of its
---chunks. In-bounds rows pass through unchanged.
---@param rows table[][]
---@param width integer
---@return table[][]
local function wrap_column_rows(rows, width)
  local out = {}
  for _, row in ipairs(rows) do
    if chunks_util.display_width(row) <= width then
      out[#out + 1] = row
    else
      for _, wrapped in ipairs(chunks_util.wrap(row, width)) do
        out[#out + 1] = wrapped
      end
    end
  end
  return out
end

---@param rows_per_column table[][][]
---@param widths integer[]
---@return table[][]
local function combine_columns(rows_per_column, widths)
  local count = #rows_per_column
  local height = 0
  for i = 1, count do
    height = math.max(height, #rows_per_column[i])
  end
  local empty_row = { { "", "Normal" } }

  local virt_lines = {}
  for row_idx = 1, height do
    local line = {}
    for col_idx = 1, count do
      local col_chunks = rows_per_column[col_idx][row_idx] or empty_row
      for _, chunk in ipairs(chunks_util.pad_to(col_chunks, widths[col_idx])) do
        line[#line + 1] = chunk
      end
      if col_idx < count then
        line[#line + 1] = { string.rep(" ", COLUMN_GAP), "Normal" }
      end
    end
    virt_lines[#virt_lines + 1] = line
  end
  return virt_lines
end

---@param active VF.Explore.Active
---@param continuation VF.Explore.InsertContinuation
---@return table[][]
local function summary_block(active, continuation)
  return {
    { { "", "Normal" } },
    { { "Explore", "Title" }, { " " .. active.label, "Comment" } },
    summary_chunks(active, continuation),
    { { "", "Normal" } },
  }
end

---@param active VF.Explore.Active
---@return integer
local function topline_row(active)
  if not v.nvim_win_is_valid(active.scratch.win) then return 0 end
  local row = math.max(0, vim.fn.line("w0", active.scratch.win) - 1)
  local line_count = math.max(1, v.nvim_buf_line_count(active.scratch.buf))
  if row >= line_count then row = line_count - 1 end
  return row
end

---Reserve `count` filler rows above the topline so virt_lines render
---even when the buffer fills the window. Without this, virt_lines on
---row 0 with `virt_lines_above` are silently dropped whenever the
---buffer is tall enough to fill the window vertically — same trick the
---play panes use (see simulate.lua's apply_virt_lines).
---@param win integer
---@param count integer
local function reserve_topfill(win, count)
  v.nvim_win_call(win, function()
    local view = vim.fn.winsaveview()
    if view.topfill ~= count then
      view.topfill = count
      vim.fn.winrestview(view)
    end
  end)
end

---@param active VF.Explore.Active
---@param continuation VF.Explore.InsertContinuation
function M.render(active, continuation)
  if not (v.nvim_buf_is_valid(active.scratch.buf)
      and v.nvim_win_is_valid(active.scratch.win)) then
    return
  end

  local columns = gather_columns(active)
  local column_rows = {}
  for i, column in ipairs(columns) do
    column_rows[i] = build_column_rows(column)
  end
  local available = math.max(MIN_COLUMN_WIDTH * #columns,
    v.nvim_win_get_width(active.scratch.win))
  local widths = compute_column_widths(column_rows, available)
  for i = 1, #column_rows do
    column_rows[i] = wrap_column_rows(column_rows[i], widths[i])
  end
  local body = combine_columns(column_rows, widths)

  local virt_lines = summary_block(active, continuation)
  for _, row in ipairs(body) do virt_lines[#virt_lines + 1] = row end
  virt_lines[#virt_lines + 1] = { { "", "Normal" } }

  local row = topline_row(active)
  v.nvim_buf_clear_namespace(active.scratch.buf, header_ns, 0, -1)
  v.nvim_buf_set_extmark(active.scratch.buf, header_ns, row, 0, {
    virt_lines = virt_lines,
    virt_lines_above = true,
  })
  reserve_topfill(active.scratch.win, #virt_lines)
end

---Re-pin the header above the current topline (called from WinScrolled /
---WinResized). Recomputes column widths against the current window width.
---@param active VF.Explore.Active
function M.reattach(active)
  M.render(active, insert_helpers.current_continuation(active))
end

---For tests: returns the per-column chunk-rows that `render` would draw
---this turn, without touching extmarks.
---@param active VF.Explore.Active
---@param continuation VF.Explore.InsertContinuation
---@return { title: string, rows: table[][] }[]
function M.column_rows_for_test(active, continuation)
  local _ = continuation
  local out = {}
  for _, column in ipairs(gather_columns(active)) do
    out[#out + 1] = { title = column.title, rows = build_column_rows(column) }
  end
  return out
end

return M
