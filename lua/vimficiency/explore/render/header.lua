-- Fixed header pane for the explore scratch buffer.
--
-- Layout:
--   (blank)
--   Explore <label>
--   Cost X.XX   Cursor (R,C)   Phase ...
--   (blank)
--   <body>
--   (blank)
--
-- Body toggles on `active.staged_mode`:
--   off: one line per column — `<title>  <full seq>` — a flat summary.
--   on:  table with one row per composition stage (Move 1, Edit 1, …);
--        every visible column sections its sequence the same way and
--        shows its stage text in that row.
--
-- Staged columns (all conform to the plan's stage structure):
--   - always: `Explored` (the session's accepted_seq)
--   - `show_result_count` optimal results, in rank order
--
-- The user's typed sequence is rendered as a SEPARATE flat line below
-- the column body (in both flat and staged modes). It does not get
-- sectioned into the plan's Move/Edit rows — the user's input doesn't
-- need to match the composition's stage structure, and forcing it into
-- those columns would be misleading.
--
-- Callers precompute `remaining` (the live-shrinking pending-insert
-- tail) so header/list stay in lockstep within one frame.
local v = vim.api
local ffi_lib = require("vimficiency.ffi")
local sections = require("vimficiency.explore.sections")

local M = {}

local header_ns = v.nvim_create_namespace("vimficiency_explore_header")
M.header_ns = header_ns

local COLUMN_SEP = "   │ "
local LABEL_WIDTH = 10  -- "Move 1" / "Edit 1" / blank — padded to this

---@param active VimficiencyExploreActive
---@param remaining string
---@return table  virt-line chunks
local function summary_chunks(active, remaining)
  local phase = active.state.phase
  local phase_label = phase.kind
  if phase.kind == "ApproachEdit" then
    phase_label = string.format("ApproachEdit %d/%d",
      phase.edit_index + 1, math.max(active.state.total_edits, 1))
  elseif phase.kind == "PendingInsert" then
    phase_label = string.format("PendingInsert '%s'", remaining)
  end
  return {
    { "Cost ", "Comment" },
    { string.format("%.2f", active.state.accepted_cost), "Normal" },
    { "   Cursor ", "Comment" },
    { string.format("(%d,%d)", active.state.cursor_row, active.state.cursor_col), "Normal" },
    { "   Phase ", "Comment" },
    { phase_label, "Normal" },
  }
end

---Gather the plan-conforming columns we show (Explored + optional
---Optimal[1..N]). User-typed is handled separately — it doesn't belong
---in a stage-structured column.
---@param active VimficiencyExploreActive
---@return { title: string, seq: string }[]
local function gather_columns(active)
  local cols = {}
  cols[#cols + 1] = { title = "Explored", seq = active.state.accepted_seq or "" }
  local optimal = (active.result and active.result.optimal_results) or {}
  local want = math.min(active.show_result_count or 0, #optimal)
  for i = 1, want do
    cols[#cols + 1] = {
      title = string.format("Optimal %d", i),
      seq = optimal[i].seq or "",
    }
  end
  return cols
end

---User-typed flat line. Rendered as a single row below the main body
---regardless of flat/staged mode. Shown only when `show_user_typed` is
---on. If the user's sequence is empty (shouldn't really happen; a
---captured session always has input), render nothing.
---@param active VimficiencyExploreActive
---@return table[]|nil  virt-line chunks or nil to suppress
local function user_typed_row(active)
  if not active.show_user_typed then return nil end
  local seq = active.result and active.result.user_seq or ""
  if seq == "" then return nil end
  return {
    { "User typed  ", "Comment" },
    { ffi_lib.format_sequence(seq), "Normal" },
  }
end

---Append padding chunk so the row's column reaches `target_width`. Only
---pads when more columns follow — trailing column's right pad is wasted.
---@param row table[]
---@param col_idx integer
---@param col_count integer
---@param target_width integer
---@param current_text string  text just written for this column
local function pad_column(row, col_idx, col_count, target_width, current_text)
  if col_idx < col_count then
    local pad = target_width - #current_text
    if pad > 0 then row[#row + 1] = { string.rep(" ", pad), "Normal" } end
  end
end

---Flat body: one line per column, `<title>  <formatted full seq>`.
---Empty `Explored` is rendered as `(start)` so the session-just-opened
---case reads naturally.
---@param cols { title: string, seq: string }[]
---@return table[][]  virt_lines
local function flat_rows(cols)
  local rows = {}
  local title_w = 0
  for _, col in ipairs(cols) do title_w = math.max(title_w, #col.title) end
  for _, col in ipairs(cols) do
    local display = col.seq ~= ""
        and ffi_lib.format_sequence(col.seq)
        or (col.title == "Explored" and "(start)" or "(none)")
    rows[#rows + 1] = {
      { string.format("%-" .. title_w .. "s  ", col.title), "Comment" },
      { display, "Normal" },
    }
  end
  return rows
end

---Staged body: one row per composition stage. Stage count = max across
---all columns; shorter columns render `(none)` for missing stages. The
---left column is a stage label (`Move N` / `Edit N`) derived from the
---stage's kind on the first column that has it.
---@param cols { title: string, seq: string }[]
---@return table[][]  virt_lines
local function staged_rows(cols)
  -- Section every column once up-front.
  local sectioned = {}
  local max_stages = 0
  for i, col in ipairs(cols) do
    sectioned[i] = sections.section_sequence(col.seq)
    max_stages = math.max(max_stages, #sectioned[i])
  end

  -- Per-column byte-width: longest stage text (or title if wider).
  local col_widths = {}
  for i, col in ipairs(cols) do
    local w = #col.title
    for _, s in ipairs(sectioned[i]) do w = math.max(w, #s.text) end
    col_widths[i] = math.max(w, 6)
  end

  local rows = {}

  -- Column-header row: blank label slot + each column's title.
  local header_row = { { string.rep(" ", LABEL_WIDTH), "Normal" } }
  for i, col in ipairs(cols) do
    if i > 1 then header_row[#header_row + 1] = { COLUMN_SEP, "Comment" } end
    header_row[#header_row + 1] = { col.title, "Title" }
    pad_column(header_row, i, #cols, col_widths[i], col.title)
  end
  rows[#rows + 1] = header_row

  if max_stages == 0 then
    rows[#rows + 1] = { { "(no stages yet)", "Comment" } }
    return rows
  end

  -- Per-stage rows. Stage kind comes from the first column that has
  -- that stage, falling back to "motion".
  local move_idx, edit_idx = 0, 0
  for stage = 1, max_stages do
    local kind
    for _, col_sections in ipairs(sectioned) do
      if col_sections[stage] then kind = col_sections[stage].kind; break end
    end
    kind = kind or "motion"
    local label
    if kind == "motion" then
      move_idx = move_idx + 1
      label = string.format("Move %d", move_idx)
    else
      edit_idx = edit_idx + 1
      label = string.format("Edit %d", edit_idx)
    end

    local row = { { string.format("%-" .. LABEL_WIDTH .. "s", label), "Comment" } }
    for i = 1, #cols do
      if i > 1 then row[#row + 1] = { COLUMN_SEP, "Comment" } end
      local section = sectioned[i][stage]
      local text = section and section.text or "(none)"
      local hl = section and "Normal" or "Comment"
      row[#row + 1] = { text, hl }
      pad_column(row, i, #cols, col_widths[i], text)
    end
    rows[#rows + 1] = row
  end
  return rows
end

---@param chunks table[]
---@return string
local function chunk_text(chunks)
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

---Render the header into its dedicated fixed-height pane.
---@param active VimficiencyExploreActive
---@param remaining string  live-computed pending-insert tail
function M.render(active, remaining)
  if not (active.header
      and v.nvim_buf_is_valid(active.header.buf)
      and v.nvim_win_is_valid(active.header.win)) then
    return
  end

  local header = {}

  header[#header + 1] = { { "", "Normal" } }
  header[#header + 1] = { { "Explore", "Title" }, { " " .. active.label, "Comment" } }
  header[#header + 1] = summary_chunks(active, remaining)
  header[#header + 1] = { { "", "Normal" } }

  local cols = gather_columns(active)
  local body = active.staged_mode and staged_rows(cols) or flat_rows(cols)
  for _, row in ipairs(body) do header[#header + 1] = row end

  local user_row = user_typed_row(active)
  if user_row then
    -- Blank separator so the user-typed line reads as a distinct band,
    -- not another column body row.
    header[#header + 1] = { { "", "Normal" } }
    header[#header + 1] = user_row
  end

  header[#header + 1] = { { "", "Normal" } }

  write_rows(active.header.buf, header)
  pcall(v.nvim_win_set_height, active.header.win, #header)
end

return M
