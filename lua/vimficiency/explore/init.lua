local config = require("vimficiency.config")
local ffi_lib = require("vimficiency.ffi")
local keynorm = require("vimficiency.capture.keynorm")
local util = require("vimficiency.util")

local M = {}

local v = vim.api
local header_ns = v.nvim_create_namespace("vimficiency_explore_header")
local tags_ns = v.nvim_create_namespace("vimficiency_explore_tags")
local on_key_ns = v.nvim_create_namespace("vimficiency_explore_on_key")

local BUFFER_OPTIONS = {
  "shiftwidth",
  "tabstop",
  "softtabstop",
  "expandtab",
  "iskeyword",
  "matchpairs",
  "virtualedit",
  "filetype",
}

local WINDOW_OPTIONS = {
  "number",
  "relativenumber",
  "cursorline",
  "wrap",
}

-- Display modes for how recommendation tags render in the scratch buffer.
-- Cycled via Tab / Shift-Tab in normal mode, or set explicitly via
-- `:Vimfy explore_mode <mode>`. Arbitrary motion keys still flow to the
-- session in all modes — only the visual overlay changes.
--
--   off        side list only.
--   highlight  rank-colored landing cells, no inline labels.
--   inplace    landing cells highlighted AND overwritten with the first
--              char of the suggested command.
--   above      landing cells highlighted, command labels on a virt_line above.
--   below      landing cells highlighted, command labels on a virt_line below.
local DISPLAY_MODES = { "off", "highlight", "inplace", "above", "below" }
local DISPLAY_MODE_SET = {}
for _, m in ipairs(DISPLAY_MODES) do DISPLAY_MODE_SET[m] = true end
local default_display_mode = "above"

-- Rank-colored highlights for landing chars + inline tag text. Each rank also
-- carries an alpha in [0.2, 0.8] mapped linearly across the ranks (best = 0.8,
-- worst = 0.2), blended against the Normal group's bg/fg at resolve time so
-- lower-ranked tags visually fade toward the background.
local RANK_HL_BASE = {
  { name = "VimficiencyExploreRank1", bg = "#4aff4a", fg = "#000000", alpha = 0.80 },
  { name = "VimficiencyExploreRank2", bg = "#b6ff4a", fg = "#000000", alpha = 0.65 },
  { name = "VimficiencyExploreRank3", bg = "#ffe24a", fg = "#000000", alpha = 0.50 },
  { name = "VimficiencyExploreRank4", bg = "#ffb04a", fg = "#000000", alpha = 0.35 },
  { name = "VimficiencyExploreRank5", bg = "#ff884a", fg = "#000000", alpha = 0.20 },
}

---@param hex string  e.g. "#4aff4a"
---@return integer, integer, integer
local function hex_to_rgb(hex)
  hex = hex:gsub("^#", "")
  return tonumber(hex:sub(1, 2), 16),
         tonumber(hex:sub(3, 4), 16),
         tonumber(hex:sub(5, 6), 16)
end

---@param int integer  a 24-bit packed RGB as returned by nvim_get_hl
---@return string  "#rrggbb"
local function int_to_hex(int)
  return string.format("#%06x", int)
end

---@param fg string  "#rrggbb"
---@param bg string  "#rrggbb"
---@param alpha number  0..1 — weight given to fg
---@return string  "#rrggbb" blended
local function blend_hex(fg, bg, alpha)
  local fr, fgn, fb = hex_to_rgb(fg)
  local br, bgn, bb = hex_to_rgb(bg)
  local r = math.floor(alpha * fr + (1 - alpha) * br + 0.5)
  local g = math.floor(alpha * fgn + (1 - alpha) * bgn + 0.5)
  local b = math.floor(alpha * fb + (1 - alpha) * bb + 0.5)
  return string.format("#%02x%02x%02x", r, g, b)
end

---Read the Normal group's bg/fg as hex strings, with sensible dark-theme fallbacks
---for the case where no truecolor or no Normal is set.
---@return string bg
---@return string fg
local function normal_colors()
  local ok, hl = pcall(v.nvim_get_hl, 0, { name = "Normal", link = false })
  local bg = (ok and hl and hl.bg) and int_to_hex(hl.bg) or "#1e1e1e"
  local fg = (ok and hl and hl.fg) and int_to_hex(hl.fg) or "#e0e0e0"
  return bg, fg
end

---Install the five rank highlight groups with alpha-blended bg/fg. Called at
---module load and again when the explore tab opens so we pick up whatever
---Normal looks like now (a colorscheme change between loads is accounted for).
---Also installs the fixed full-opacity target highlight, which intentionally
---does NOT blend so the goal catches the eye at maximum saturation.
local TARGET_HL = "VimficiencyExploreTarget"
local function refresh_rank_highlights()
  local base_bg, base_fg = normal_colors()
  for _, entry in ipairs(RANK_HL_BASE) do
    v.nvim_set_hl(0, entry.name, {
      bg = blend_hex(entry.bg, base_bg, entry.alpha),
      fg = blend_hex(entry.fg, base_fg, entry.alpha),
      default = true,
    })
  end
  -- Background-only: the underlying buffer character keeps its native fg so
  -- `main` stays as `main` against blue rather than getting painted over.
  v.nvim_set_hl(0, TARGET_HL, { bg = "#2a7fff", default = true })
end
refresh_rank_highlights()

local function rank_hl(rank)
  local entry = RANK_HL_BASE[rank] or RANK_HL_BASE[#RANK_HL_BASE]
  return entry.name
end

---@class VimficiencyExploreActive
---@field label string
---@field result ResultSession
---@field generation integer
---@field source_buf integer
---@field source_win integer
---@field source_tab integer
---@field scratch_buf integer
---@field scratch_win integer
---@field scratch_tab integer
---@field list_buf integer
---@field list_win integer
---@field state VimficiencyExploreState
---@field recommendations VimficiencyExploreRecommendation[]
---@field on_key_buffer string
---@field display_mode string  # one of "off" | "inplace" | "above" | "below"

---@type VimficiencyExploreActive|nil
local active = nil

local function assert_active()
  assert(active, "vimficiency explore session is not active")
  return active
end

local function copy_buffer_options(src_buf, scratch_buf)
  for _, opt in ipairs(BUFFER_OPTIONS) do
    local ok, value = pcall(v.nvim_get_option_value, opt, { buf = src_buf })
    if ok then
      pcall(v.nvim_set_option_value, opt, value, { buf = scratch_buf })
    end
  end
end

local function copy_window_options(src_win, scratch_win)
  for _, opt in ipairs(WINDOW_OPTIONS) do
    local ok, value = pcall(v.nvim_get_option_value, opt, { win = src_win })
    if ok then
      pcall(v.nvim_set_option_value, opt, value, { win = scratch_win })
    end
  end
end

---@param result ResultSession
---@return boolean
---@return string|nil
local function ensure_explore_metadata(result)
  if type(result.goal_lines) ~= "table" or #result.goal_lines == 0 then
    return false, "result is missing goal_lines; re-run the session with a newer vimficiency build"
  end
  if type(result.has_lines_above) ~= "boolean" or type(result.has_lines_below) ~= "boolean" then
    return false, "result is missing explore boundary metadata; re-run the session with a newer vimficiency build"
  end
  return true, nil
end

---@param lines string[]
---@param goal_lines string[]
---@return integer
local function compute_boundary_last_col(lines, goal_lines)
  local current_last = #(lines[#lines] or "")
  local goal_last = #(goal_lines[#goal_lines] or "")
  return math.max(0, math.max(current_last, goal_last) - 1)
end

local function clear_on_key_buffer()
  if active then active.on_key_buffer = "" end
end

local function sync_cursor_to_state()
  local a = assert_active()
  v.nvim_win_set_cursor(a.scratch_win, { a.state.cursor_row + 1, a.state.cursor_col })
end

local function refresh_state_and_recommendations()
  local a = assert_active()
  a.state = ffi_lib.explore_state(a.generation)
  a.recommendations = ffi_lib.explore_recommendations(a.generation, config.EXPLORE_RECOMMENDATIONS)
end

local function render_header()
  local a = assert_active()
  local phase = a.state.phase
  local phase_label = phase.kind
  if phase.kind == "ApproachEdit" then
    phase_label = string.format("ApproachEdit %d/%d",
      phase.edit_index + 1, math.max(a.state.total_edits, 1))
  elseif phase.kind == "PendingInsert" then
    phase_label = string.format("PendingInsert '%s'", phase.remaining_typed_text)
  end

  local header = {
    { { "", "Normal" } },
    { { "Explore", "Title" }, { " " .. a.label, "Comment" } },
    { { "User typed ", "Comment" }, { ffi_lib.format_sequence(a.result.user_seq or ""), "Normal" } },
    {
      { "Explored ", "Comment" },
      { a.state.accepted_seq ~= "" and ffi_lib.format_sequence(a.state.accepted_seq) or "(start)", "Normal" },
    },
    {
      { "Cost ", "Comment" },
      { string.format("%.2f", a.state.accepted_cost), "Normal" },
      { "   Cursor ", "Comment" },
      { string.format("(%d,%d)", a.state.cursor_row, a.state.cursor_col), "Normal" },
      { "   Phase ", "Comment" },
      { phase_label, "Normal" },
    },
  }
  header[#header + 1] = { { "", "Normal" } }

  v.nvim_buf_clear_namespace(a.scratch_buf, header_ns, 0, -1)
  v.nvim_buf_set_extmark(a.scratch_buf, header_ns, 0, 0, {
    virt_lines = header,
    virt_lines_above = true,
    virt_lines_leftcol = true,
    priority = 2200,
  })

  v.nvim_win_call(a.scratch_win, function()
    local view = vim.fn.winsaveview()
    view.topline = 1
    view.topfill = #header
    vim.fn.winrestview(view)
  end)
end

local function render_recommendation_list()
  local a = assert_active()
  if not v.nvim_buf_is_valid(a.list_buf) then return end

  local lines = {
    "Recommendations",
    "",
  }
  local hl_entries = {}

  if #a.recommendations == 0 then
    lines[#lines + 1] = "(none)"
  else
    for i, item in ipairs(a.recommendations) do
      local kind_label = item.kind == "motion" and "move" or item.kind
      local tail = string.format("  %.2f  %s", item.total_path_cost, kind_label)
      if item.kind == "motion" then
        tail = tail .. string.format("  -> (%d,%d)", item.landing_row, item.landing_col)
      end
      local line = string.format(
        "%d. %s%s",
        i,
        ffi_lib.format_sequence(item.text),
        tail)
      lines[#lines + 1] = line
      hl_entries[#hl_entries + 1] = { row = #lines - 1, hl = rank_hl(i) }
    end
  end

  vim.bo[a.list_buf].modifiable = true
  v.nvim_buf_set_lines(a.list_buf, 0, -1, false, lines)
  vim.bo[a.list_buf].modifiable = false

  v.nvim_buf_clear_namespace(a.list_buf, tags_ns, 0, -1)
  for _, entry in ipairs(hl_entries) do
    -- Highlight only the leading "N." rank marker so colors don't wash out the whole line.
    v.nvim_buf_set_extmark(a.list_buf, tags_ns, entry.row, 0, {
      end_col = 2,
      hl_group = entry.hl,
      priority = 2100,
    })
  end
end

---@param recs VimficiencyExploreRecommendation[]
---@return table<integer, VimficiencyExploreRecommendation[]>
local function group_motions_by_row(recs)
  local by_row = {}
  for _, item in ipairs(recs) do
    if item.kind == "motion" then
      local list = by_row[item.landing_row]
      if not list then
        list = {}
        by_row[item.landing_row] = list
      end
      list[#list + 1] = item
    end
  end
  return by_row
end

---Return the list of cells {row, col} the current target occupies. Half-open
---range: `[begin, end)`. Zero-width targets (pure insertion points) are
---represented by a single `{zero_width = true}` cell at begin.
local function target_cells(state, scratch_buf)
  local br, bc = state.target_begin_row, state.target_begin_col
  local er, ec = state.target_end_row, state.target_end_col
  if br < 0 then return {} end
  local function line_len(row)
    local line = v.nvim_buf_get_lines(scratch_buf, row, row + 1, false)[1] or ""
    return #line
  end
  local cells = {}
  if br == er then
    if bc == ec then
      cells[#cells + 1] = { row = br, col = bc, zero_width = true }
    else
      for c = bc, ec - 1 do cells[#cells + 1] = { row = br, col = c } end
    end
  else
    -- Multi-row: begin row from bc to EOL, intermediate rows fully, end row 0..ec.
    for c = bc, math.max(bc, line_len(br) - 1) do
      cells[#cells + 1] = { row = br, col = c }
    end
    for r = br + 1, er - 1 do
      for c = 0, math.max(0, line_len(r) - 1) do
        cells[#cells + 1] = { row = r, col = c }
      end
    end
    for c = 0, ec - 1 do cells[#cells + 1] = { row = er, col = c } end
  end
  return cells
end

---Paint the current-edit target in full-opacity blue. Priority 2300 so it
---sits above rank-colored landing cells; target cells subsume any coincident
---rank landing. Zero-width targets render a caret-style marker.
local function render_target_highlight()
  local a = assert_active()
  local cells = target_cells(a.state, a.scratch_buf)
  for _, cell in ipairs(cells) do
    if cell.zero_width then
      v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, cell.row, cell.col, {
        virt_text = { { "\u{2502}", TARGET_HL } },  -- │
        virt_text_pos = "overlay",
        priority = 2300,
      })
    else
      local line = v.nvim_buf_get_lines(a.scratch_buf, cell.row, cell.row + 1, false)[1] or ""
      if cell.col >= #line then
        v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, cell.row, math.max(0, #line), {
          virt_text = { { " ", TARGET_HL } },
          virt_text_pos = "overlay",
          priority = 2300,
        })
      else
        v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, cell.row, cell.col, {
          end_col = cell.col + 1,
          hl_group = TARGET_HL,
          priority = 2300,
        })
      end
    end
  end
end

---Per-row claim map used by the "no adjacent highlights" rule. Each entry
---marks a (row, col) pair as unavailable for a further highlight or tag; the
---claim naturally extends 1 cell on each side of every placed element, so any
---two claimed highlights sit at least 2 columns apart visually.
local function make_claims() return {} end

local function claim_cell(claims, row, col)
  claims[row] = claims[row] or {}
  claims[row][col] = true
end

---Claim `[col - 1, col + width]` on `row` — i.e. the placed span plus a
---1-cell gap on each side.
local function claim_with_gap(claims, row, col, width)
  for c = col - 1, col + width do claim_cell(claims, row, c) end
end

local function is_claimed(claims, row, col)
  return claims[row] and claims[row][col] == true
end

---Build a set of target cell keys for quick membership tests (row:col -> true).
local function target_cell_set(state, scratch_buf)
  local set = {}
  for _, cell in ipairs(target_cells(state, scratch_buf)) do
    set[cell.row .. ":" .. cell.col] = true
  end
  return set
end

---Highlight the landing cell of every motion recommendation with its rank
---color, applying the "no adjacent highlights" rule between ranks. The target
---does NOT participate in the claim map — the blue target backdrop always wins
---visually because its priority is higher, and we want to allow rank landings
---to sit directly on or next to the target so the user can see which motion
---reaches the goal.
local function render_landing_highlights()
  local a = assert_active()
  local claims = make_claims()

  local motions = {}
  for _, item in ipairs(a.recommendations) do
    if item.kind == "motion" then motions[#motions + 1] = item end
  end
  table.sort(motions, function(x, y) return (x.rank or 0) < (y.rank or 0) end)

  for _, item in ipairs(motions) do
    local row = item.landing_row
    local col = item.landing_col
    if not is_claimed(claims, row, col) then
      claim_with_gap(claims, row, col, 1)
      local line = v.nvim_buf_get_lines(a.scratch_buf, row, row + 1, false)[1] or ""
      if col >= #line then
        v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, row, math.max(0, #line), {
          virt_text = { { " ", rank_hl(item.rank or 0) } },
          virt_text_pos = "overlay",
          priority = 2050,
        })
      else
        v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, row, col, {
          end_col = col + 1,
          hl_group = rank_hl(item.rank or 0),
          priority = 2050,
        })
      end
    end
  end
  return claims
end

---Mode `inplace`: overwrite the landing cell with the first char of the
---command via a virt_text overlay. Non-target cells use the rank's hl (rank
---bg + rank fg); target cells use TARGET_HL so the blue backdrop is preserved
---while the char switches to the rank's command char — e.g. `main` under the
---goal might render as `wain` when rank 1 `w` lands on the `m`.
local function render_tags_inplace()
  local a = assert_active()
  local claims = make_claims()
  local target_set = target_cell_set(a.state, a.scratch_buf)

  local motions = {}
  for _, item in ipairs(a.recommendations) do
    if item.kind == "motion" and item.text ~= "" then motions[#motions + 1] = item end
  end
  table.sort(motions, function(x, y) return (x.rank or 0) < (y.rank or 0) end)

  for _, item in ipairs(motions) do
    local row = item.landing_row
    local col = item.landing_col
    if not is_claimed(claims, row, col) then
      claim_with_gap(claims, row, col, 1)
      local first_char = ffi_lib.format_sequence(item.text):sub(1, 1)
      local on_target = target_set[row .. ":" .. col] == true
      local hl = on_target and TARGET_HL or rank_hl(item.rank or 0)
      v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, row, col, {
        virt_text = { { first_char, hl } },
        virt_text_pos = "overlay",
        priority = 2320,  -- above target bg so overlay char is visible
      })
    end
  end
end

---@param text string
---@return integer  Number of display cells (utf-8 char count; superscripts and
--- ASCII are both 1 cell wide).
local function display_width(text)
  local width = 0
  for _ in text:gmatch("[%z\1-\127\194-\244][\128-\191]*") do width = width + 1 end
  return width
end

---Build one virt_line of tag chunks for the recs landing on `row`.
---Rank-first placement: higher-ranked tags claim their columns first, so
---lower-ranked tags are the ones that get hidden on overlap — matching the
---rule "two motions to the same visual location → hide the lower-ranking one,
---keep it in the side list."
---@return table  chunks list (possibly empty) for the virt_lines extmark
local function build_row_tag_line(items)
  table.sort(items, function(a_, b_) return (a_.rank or 0) < (b_.rank or 0) end)

  local claimed = {}
  local placed = {}
  for _, item in ipairs(items) do
    local tag_text = ffi_lib.format_sequence(item.text)
    local start_col = item.landing_col
    local width = display_width(tag_text)

    -- Require 1-col gap on each side: check [start_col - 1, start_col + width].
    local overlap = false
    for c = start_col - 1, start_col + width do
      if claimed[c] then overlap = true; break end
    end
    if not overlap then
      for c = start_col - 1, start_col + width do claimed[c] = true end
      placed[#placed + 1] = { col = start_col, text = tag_text, rank = item.rank or 0, width = width }
    end
  end

  table.sort(placed, function(a_, b_) return a_.col < b_.col end)

  local chunks = {}
  local written_col = 0
  for _, p in ipairs(placed) do
    local pad = p.col - written_col
    if pad > 0 then
      chunks[#chunks + 1] = { string.rep(" ", pad), "Normal" }
    end
    chunks[#chunks + 1] = { p.text, rank_hl(p.rank) }
    written_col = p.col + p.width
  end
  return chunks
end

---Mode `above` / `below`: emit a virt_lines extmark on EVERY buffer row so the
---vertical rhythm stays consistent. Rows with landings carry their tag chunks;
---rows without landings get a single empty chunk so the slot still renders.
local function render_tags_virt_lines(above)
  local a = assert_active()
  local by_row = group_motions_by_row(a.recommendations)
  local num_lines = v.nvim_buf_line_count(a.scratch_buf)
  for row = 0, num_lines - 1 do
    local items = by_row[row]
    local chunks = (items and #items > 0) and build_row_tag_line(items)
                                           or { { "", "Normal" } }
    v.nvim_buf_set_extmark(a.scratch_buf, tags_ns, row, 0, {
      virt_lines = { chunks },
      virt_lines_above = above,
      virt_lines_leftcol = false,
      priority = 2100,
    })
  end
end

---Dispatch to per-mode rendering. The target highlight (when present) is
---always drawn first — except in `off`, where the buffer stays fully clean.
local function render_motion_tags()
  local a = assert_active()
  v.nvim_buf_clear_namespace(a.scratch_buf, tags_ns, 0, -1)

  local mode = a.display_mode or default_display_mode
  if mode == "off" then return end

  render_target_highlight()
  if mode == "highlight" then
    render_landing_highlights()
  elseif mode == "inplace" then
    render_landing_highlights()
    render_tags_inplace()
  elseif mode == "below" then
    render_landing_highlights()
    render_tags_virt_lines(false)
  else  -- default & "above"
    render_landing_highlights()
    render_tags_virt_lines(true)
  end
end

-- For convenience, add a rank field to each recommendation so tag rendering
-- can find them after sorting.
local function attach_ranks()
  local a = assert_active()
  for i, rec in ipairs(a.recommendations) do
    rec.rank = i
  end
end

local function refresh_ui()
  refresh_state_and_recommendations()
  attach_ranks()
  sync_cursor_to_state()
  render_header()
  render_recommendation_list()
  render_motion_tags()
end

---Rewrite the scratch buffer to match the session's current lines. The buffer
---stays modifiable throughout — natural edit commands must be able to fire
---between sync calls.
local function sync_buffer_from_session()
  local a = assert_active()
  local lines = ffi_lib.explore_current_lines(a.generation)
  v.nvim_buf_set_lines(a.scratch_buf, 0, -1, false, lines)
  vim.bo[a.scratch_buf].modified = false
end

local function undo()
  clear_on_key_buffer()
  local a = assert_active()
  local result = ffi_lib.explore_undo(a.generation)
  if result.status == "Rejected" then
    vim.notify("vimficiency explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  sync_buffer_from_session()
  refresh_ui()
  return true
end

local function redo()
  clear_on_key_buffer()
  local a = assert_active()
  local result = ffi_lib.explore_redo(a.generation)
  if result.status == "Rejected" then
    vim.notify("vimficiency explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  sync_buffer_from_session()
  refresh_ui()
  return true
end

local function on_cursor_moved()
  if not active then return end
  -- Loop-avoidance: if nvim cursor already matches session cursor, we just set it
  -- ourselves (e.g. after apply_motion / undo / redo) — nothing to forward.
  local pos = v.nvim_win_get_cursor(active.scratch_win)
  local new_row, new_col = pos[1] - 1, pos[2]
  if new_row == active.state.cursor_row and new_col == active.state.cursor_col then
    return
  end

  local raw_keys = active.on_key_buffer or ""
  active.on_key_buffer = ""
  local applied = ffi_lib.explore_accept_cursor_move(
    active.generation, new_row, new_col, raw_keys)
  if applied.status == "Rejected" then
    -- Session refused the external move — snap cursor back to the session's view.
    sync_cursor_to_state()
    return
  end
  refresh_ui()
end

---Strict-revert buffer-state handler. Called on TextChanged (normal mode) and
---InsertLeave. Compares the live scratch buffer to the session's view and
---forwards via `acceptBufferState`. If the session rejects (user drifted
---outside the planned edit scope), the buffer is reverted to the session's
---last-known lines + cursor; otherwise the phase/cursor/lines advance per
---the session's decision and the UI refreshes.
local function on_buffer_changed()
  if not active then return end
  local a = active
  local new_lines = v.nvim_buf_get_lines(a.scratch_buf, 0, -1, false)
  local pos = v.nvim_win_get_cursor(a.scratch_win)
  local new_row, new_col = pos[1] - 1, pos[2]

  -- If the buffer already matches the session's lines (e.g. we just
  -- programmatically wrote them via sync_buffer_from_session), there's
  -- nothing to forward — on_cursor_moved handles any cursor drift.
  local session_lines = ffi_lib.explore_current_lines(a.generation)
  local buffer_matches_session = (#new_lines == #session_lines)
  if buffer_matches_session then
    for i = 1, #new_lines do
      if new_lines[i] ~= session_lines[i] then
        buffer_matches_session = false
        break
      end
    end
  end
  if buffer_matches_session then return end

  local raw_keys = a.on_key_buffer or ""
  a.on_key_buffer = ""

  local applied = ffi_lib.explore_accept_buffer_state(
    a.generation, new_lines, new_row, new_col, raw_keys)

  if applied.status == "Rejected" then
    -- Strict (b): revert the scratch to the session's last known state + cursor.
    vim.notify("vimficiency explore: " .. applied.reason, vim.log.levels.WARN)
    sync_buffer_from_session()
    sync_cursor_to_state()
    return
  end
  refresh_ui()
end

local function destroy_active_and_tab()
  if not active then return end
  local tab = active.scratch_tab
  vim.on_key(nil, on_key_ns)
  ffi_lib.explore_destroy(active.generation)
  active = nil
  vim.schedule(function()
    if tab and v.nvim_tabpage_is_valid(tab) then
      pcall(function()
        v.nvim_set_current_tabpage(tab)
        vim.cmd("tabclose")
      end)
    end
  end)
end

function M.open(label, result, opts)
  assert(type(label) == "string" and label ~= "", "explore.open: label must be non-empty")
  assert(type(result) == "table", "explore.open: result must be a ResultSession")

  -- Re-blend against the current Normal group in case the colorscheme changed.
  refresh_rank_highlights()

  local ok, err = ensure_explore_metadata(result)
  if not ok then
    vim.notify("vimficiency explore failed: " .. err, vim.log.levels.ERROR)
    return false
  end

  if active then
    M.cancel()
  end

  opts = opts or {}

  local source_buf = v.nvim_get_current_buf()
  local source_win = v.nvim_get_current_win()
  local source_tab = v.nvim_get_current_tabpage()

  local initial_lines = result.lines or {}
  local start_row = result.start_row or 0
  local start_col = result.start_col or 0
  local window_height = v.nvim_win_get_height(source_win)
  local scroll_amount = v.nvim_get_option_value("scroll", { win = source_win })
  local boundary_last_col = compute_boundary_last_col(initial_lines, result.goal_lines)

  local generation = ffi_lib.explore_start(
    initial_lines, start_row, start_col,
    result.goal_lines, result.end_row or 0, result.end_col or 0,
    0, boundary_last_col,
    result.has_lines_above, result.has_lines_below,
    window_height, scroll_amount,
    result.user_seq or "")

  vim.cmd("tabnew")
  local scratch_tab = v.nvim_get_current_tabpage()
  local scratch_win = v.nvim_get_current_win()
  local tabnew_buf = v.nvim_get_current_buf()
  local scratch_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(scratch_win, scratch_buf)
  if v.nvim_buf_is_valid(tabnew_buf) then
    pcall(v.nvim_buf_delete, tabnew_buf, { force = true })
  end

  v.nvim_buf_set_name(scratch_buf, "vimficiency://explore/" .. label)
  v.nvim_buf_set_lines(scratch_buf, 0, -1, false, initial_lines)

  vim.bo[scratch_buf].buftype = "nofile"
  vim.bo[scratch_buf].bufhidden = "wipe"
  vim.bo[scratch_buf].swapfile = false
  vim.bo[scratch_buf].undofile = false
  -- undolevels = -1 disables Vim's native undo so the session is authoritative
  -- about state changes, but the buffer itself stays modifiable so natural
  -- edit commands (r{char}, x, s, c{motion}, etc.) flow through unimpeded.
  vim.bo[scratch_buf].undolevels = -1
  vim.bo[scratch_buf].modifiable = true
  vim.bo[scratch_buf].modified = false

  copy_buffer_options(source_buf, scratch_buf)
  copy_window_options(source_win, scratch_win)

  -- List panel on the LEFT.
  vim.cmd("topleft vsplit")
  local list_win = v.nvim_get_current_win()
  local list_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(list_win, list_buf)
  v.nvim_win_set_width(list_win, 44)
  vim.bo[list_buf].buftype = "nofile"
  vim.bo[list_buf].bufhidden = "wipe"
  vim.bo[list_buf].swapfile = false
  vim.bo[list_buf].modifiable = false
  vim.bo[list_buf].filetype = "vimficiency"
  vim.wo[list_win].number = false
  vim.wo[list_win].relativenumber = false
  vim.wo[list_win].cursorline = false
  vim.wo[list_win].wrap = false
  v.nvim_set_current_win(scratch_win)

  active = {
    label = label,
    result = result,
    generation = generation,
    source_buf = source_buf,
    source_win = source_win,
    source_tab = source_tab,
    scratch_buf = scratch_buf,
    scratch_win = scratch_win,
    scratch_tab = scratch_tab,
    list_buf = list_buf,
    list_win = list_win,
    state = {
      phase = { kind = "ApproachEdit", edit_index = 0, remaining_typed_text = "" },
      cursor_row = start_row,
      cursor_col = start_col,
      total_edits = 0,
      accepted_cost = 0,
      accepted_seq = "",
      accepted_revision = 0,
      can_undo = false,
      can_redo = false,
    },
    recommendations = {},
    on_key_buffer = "",
    display_mode = default_display_mode,
  }

  -- Capture raw key bytes only while this session is open. We can't prevent
  -- Vim from handling the key — we only observe it so that CursorMoved can
  -- forward a motion with the text that produced it. The normalization step
  -- is shared with the regular mark/watch/recall capture — see
  -- dev/lua/key-normalization.md.
  vim.on_key(function(_, typed)
    if not (active and typed and typed ~= "") then return end
    active.on_key_buffer = active.on_key_buffer .. keynorm.normalize(typed)
  end, on_key_ns)

  util.set_buffer_keymaps(scratch_buf, {
    { lhs = "q",    handler = function() M.cancel() end, desc = "Close explore session", nowait = true },
    { lhs = "u",    handler = undo,                      desc = "Undo explore step",     nowait = true },
    { lhs = "<C-u>",handler = undo,                      desc = "Undo explore step",     nowait = true },
    { lhs = "<C-r>",handler = redo,                      desc = "Redo explore step",     nowait = true },
    { lhs = "<Tab>",   handler = function() clear_on_key_buffer(); M.cycle_display_mode(1) end,  desc = "Next explore display mode",     mode = "n", nowait = true },
    { lhs = "<S-Tab>", handler = function() clear_on_key_buffer(); M.cycle_display_mode(-1) end, desc = "Previous explore display mode", mode = "n", nowait = true },
  })

  util.set_buffer_keymaps(list_buf, {
    { lhs = "q", handler = function() M.cancel() end, desc = "Close explore session", nowait = true },
  })

  -- CursorMoved forwards any natural motion to the session.
  v.nvim_create_autocmd("CursorMoved", {
    buffer = scratch_buf,
    callback = on_cursor_moved,
    desc = "vimficiency explore: forward cursor move to session",
  })

  -- TextChanged fires after a normal-mode edit completes (e.g. `rm`, `x`).
  -- InsertLeave fires after the user exits insert — the right moment to
  -- validate insert-mode edits (`sm<Esc>`, `cl m<Esc>`). TextChangedI is
  -- deliberately NOT hooked: buffer state mid-typing isn't a valid target.
  v.nvim_create_autocmd({ "TextChanged", "InsertLeave" }, {
    buffer = scratch_buf,
    callback = on_buffer_changed,
    desc = "vimficiency explore: validate buffer state vs. planned fencepost",
  })

  -- Either window closing tears down the whole session.
  local function on_window_close()
    if not active then return end
    destroy_active_and_tab()
  end
  v.nvim_create_autocmd("WinClosed", {
    pattern = tostring(scratch_win),
    once = true,
    callback = on_window_close,
    desc = "vimficiency explore: close when scratch window closes",
  })
  v.nvim_create_autocmd("WinClosed", {
    pattern = tostring(list_win),
    once = true,
    callback = on_window_close,
    desc = "vimficiency explore: close when list window closes",
  })

  refresh_ui()
  if opts.focus == false and v.nvim_tabpage_is_valid(source_tab) then
    v.nvim_set_current_tabpage(source_tab)
  end
  vim.notify("vimficiency explore opened [" .. label .. "]", vim.log.levels.INFO)
  return true
end

function M.cancel()
  if not active then
    vim.notify("vimficiency explore not active", vim.log.levels.WARN)
    return false
  end
  destroy_active_and_tab()
  vim.notify("vimficiency explore cancelled", vim.log.levels.INFO)
  return true
end

---Set the tag display mode. If a session is active, re-render immediately.
---The chosen mode also becomes the default for subsequently-opened sessions.
---@param mode string  # "off" | "inplace" | "above" | "below"
---@return boolean
function M.set_display_mode(mode)
  if not DISPLAY_MODE_SET[mode] then
    vim.notify("vimficiency explore: invalid mode '" .. tostring(mode) ..
      "' (expected off|inplace|above|below)", vim.log.levels.ERROR)
    return false
  end
  default_display_mode = mode
  if active then
    active.display_mode = mode
    render_motion_tags()
  end
  vim.notify("vimficiency explore display mode: " .. mode, vim.log.levels.INFO)
  return true
end

---Cycle through the display modes in `DISPLAY_MODES` order. Positive `step`
---advances, negative steps back.
---@param step integer|nil  default 1
function M.cycle_display_mode(step)
  step = step or 1
  local current = (active and active.display_mode) or default_display_mode
  local idx = 1
  for i, m in ipairs(DISPLAY_MODES) do
    if m == current then idx = i; break end
  end
  local n = #DISPLAY_MODES
  local next_idx = ((idx - 1 + step) % n + n) % n + 1
  return M.set_display_mode(DISPLAY_MODES[next_idx])
end

---@return string[]
function M.list_display_modes()
  return vim.deepcopy(DISPLAY_MODES)
end

function M.status()
  if not active then return nil end
  return {
    phase = vim.deepcopy(active.state.phase),
    accepted_seq = active.state.accepted_seq,
    accepted_cost = active.state.accepted_cost,
    recommendations = vim.deepcopy(active.recommendations),
  }
end

return M
