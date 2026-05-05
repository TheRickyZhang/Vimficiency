-- Motion / edit tag overlay for the explore scratch buffer.
--
-- Five display modes, dispatched by `render(active)` off `active.display_mode`:
--
--   off        clean buffer — side list only.
--   highlight  rank-colored landing cells, no inline labels.
--   inplace    landing cell overwritten with the first char of the command.
--   above      landing cells highlighted + command labels on a virt_line above.
--   below      same but below.
--
-- The module owns `tags_ns` (shared with the recommendation list view — the
-- extmarks sit on different buffers, so a single namespace keeps clearing
-- symmetrical). Every call to `render` starts by clearing the scratch
-- buffer's tags_ns extmarks.
local v = vim.api
local highlights = require("vimficiency.explore.highlights")
local sequence_display = require("vimficiency.sequence_display")

local M = {}

local tags_ns = v.nvim_create_namespace("vimfy_explore_tags")
M.tags_ns = tags_ns
M.TARGET_HL = highlights.TARGET_HL

local rank_hl = highlights.rank_hl

local function item_rank(item)
  return assert(item.rank, "vimfy explore: recommendation missing rank")
end

---@param recs VF.Explore.Recommendation[]
---@return table<integer, VF.Explore.Recommendation[]>
local function group_motions_by_row(recs)
  -- Caller must have already gated on Navigate phase: in Navigate phase
  -- every rec is a motion atom whose landing cell is meaningful for the
  -- tags overlay.
  local by_row = {}
  for _, item in ipairs(recs) do
    local list = by_row[item.landing.row]
    if not list then
      list = {}
      by_row[item.landing.row] = list
    end
    list[#list + 1] = item
  end
  return by_row
end

---Return the list of cells {row, col} the current target occupies. Half-open
---range: `[begin, end)`. Zero-width targets (pure insertion points) are
---represented by a single `{zero_width = true}` cell at begin.
local function target_cells(state, scratch_buf)
  if state.phase.kind == "Insert" then return {} end
  local br, bc = state.target_range.begin_pos.row, state.target_range.begin_pos.col
  local er, ec = state.target_range.end_pos.row, state.target_range.end_pos.col
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
M.target_cells = target_cells

---Paint the current-edit target in full-opacity blue. Priority 2300 so it
---sits above rank-colored landing cells; target cells subsume any coincident
---rank landing. Zero-width targets render a caret-style marker.
local function render_target_highlight(active)
  local cells = target_cells(active.state, active.scratch.buf)
  for _, cell in ipairs(cells) do
    if cell.zero_width then
      v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, cell.row, cell.col, {
        virt_text = { { "\u{2502}", highlights.TARGET_HL } },
        virt_text_pos = "overlay",
        priority = 2300,
      })
    else
      local line = v.nvim_buf_get_lines(active.scratch.buf, cell.row, cell.row + 1, false)[1] or ""
      if cell.col >= #line then
        v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, cell.row, math.max(0, #line), {
          virt_text = { { " ", highlights.TARGET_HL } },
          virt_text_pos = "overlay",
          priority = 2300,
        })
      else
        v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, cell.row, cell.col, {
          end_col = cell.col + 1,
          hl_group = highlights.TARGET_HL,
          priority = 2300,
        })
      end
    end
  end
end

-- Per-row claim map: no-adjacent-highlights rule between ranks.
local function make_claims() return {} end
local function claim_cell(claims, row, col)
  claims[row] = claims[row] or {}
  claims[row][col] = true
end
local function claim_with_gap(claims, row, col, width)
  for c = col - 1, col + width do claim_cell(claims, row, c) end
end
local function is_claimed(claims, row, col)
  return claims[row] and claims[row][col] == true
end

local function target_cell_set(state, scratch_buf)
  local set = {}
  for _, cell in ipairs(target_cells(state, scratch_buf)) do
    set[cell.row .. ":" .. cell.col] = true
  end
  return set
end

---Highlight the landing cell of every motion recommendation with its rank
---color. `filter` (optional): when present, only items in the set are
---highlighted — used by above/below so a dropped label also drops its cell.
local function render_landing_highlights(active, filter)
  local claims = make_claims()
  local motions = {}
  for _, item in ipairs(active.recommendations) do
    if not filter or filter[item] then
      motions[#motions + 1] = item
    end
  end
  table.sort(motions, function(x, y) return item_rank(x) < item_rank(y) end)

  for _, item in ipairs(motions) do
    local row = item.landing.row
    local col = item.landing.col
    if not is_claimed(claims, row, col) then
      claim_with_gap(claims, row, col, 1)
      local line = v.nvim_buf_get_lines(active.scratch.buf, row, row + 1, false)[1] or ""
      if col >= #line then
        v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, row, math.max(0, #line), {
          virt_text = { { " ", rank_hl(item_rank(item)) } },
          virt_text_pos = "overlay",
          priority = 2050,
        })
      else
        v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, row, col, {
          end_col = col + 1,
          hl_group = rank_hl(item_rank(item)),
          priority = 2050,
        })
      end
    end
  end
end

---Mode `inplace`: overwrite the landing cell with the first char of the
---command via a virt_text overlay.
local function render_tags_inplace(active)
  local claims = make_claims()
  local target_set = target_cell_set(active.state, active.scratch.buf)

  local motions = {}
  for _, item in ipairs(active.recommendations) do
    if item.text ~= "" then motions[#motions + 1] = item end
  end
  table.sort(motions, function(x, y) return item_rank(x) < item_rank(y) end)

  for _, item in ipairs(motions) do
    local row = item.landing.row
    local col = item.landing.col
    if not is_claimed(claims, row, col) then
      claim_with_gap(claims, row, col, 1)
      local first_char = sequence_display.inline(item.text):sub(1, 1)
      local on_target = target_set[row .. ":" .. col] == true
      local hl = on_target and highlights.TARGET_HL or rank_hl(item_rank(item))
      v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, row, col, {
        virt_text = { { first_char, hl } },
        virt_text_pos = "overlay",
        priority = 2320,
      })
    end
  end
end

---Number of display cells in a UTF-8 string (superscripts and ASCII both 1).
local function display_width(text)
  local width = 0
  for _ in text:gmatch("[%z\1-\127\194-\244][\128-\191]*") do width = width + 1 end
  return width
end

---Build one virt_line of tag chunks for the recs landing on `row`. Rank-
---first placement — higher-ranked tags claim columns first; lower-ranked
---ones get hidden on overlap. `placed_out` (optional) collects items that
---actually made it onto the line, so callers can filter the landing-
---highlight pass to match.
local function build_row_tag_line(items, placed_out)
  table.sort(items, function(a_, b_) return item_rank(a_) < item_rank(b_) end)

  local claimed = {}
  local placed = {}
  for _, item in ipairs(items) do
    local tag_text = sequence_display.inline(item.text)
    local start_col = item.landing.col
    local width = display_width(tag_text)

    local overlap = false
    for c = start_col - 1, start_col + width do
      if claimed[c] then overlap = true; break end
    end
    if not overlap then
      for c = start_col, start_col + width - 1 do claimed[c] = true end
      placed[#placed + 1] = { col = start_col, text = tag_text, rank = item_rank(item), width = width }
      if placed_out then placed_out[item] = true end
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

---Mode `above` / `below`: emit a virt_lines extmark on EVERY buffer row so
---the vertical rhythm stays consistent. Returns the set of items whose label
---was placed (so landing-highlight can be filtered to match).
local function render_tags_virt_lines(active, above)
  local by_row = group_motions_by_row(active.recommendations)
  local placed_items = {}
  local num_lines = v.nvim_buf_line_count(active.scratch.buf)
  for row = 0, num_lines - 1 do
    local items = by_row[row]
    local chunks = (items and #items > 0) and build_row_tag_line(items, placed_items)
                                           or { { "", "Normal" } }
    v.nvim_buf_set_extmark(active.scratch.buf, tags_ns, row, 0, {
      virt_lines = { chunks },
      virt_lines_above = above,
      virt_lines_leftcol = false,
      priority = 2100,
    })
  end
  return placed_items
end

---Dispatch to the mode-specific renderer. Target highlight always draws
---first (except in `off`), so it sits under the rank-colored landings.
---
---Motion-tag overlays only make sense in Navigate phase, where every rec
---is a single-token motion with a meaningful landing cell. Transform/Insert
---phases get the target-range highlight only — their recs aren't motions.
---@param active VF.Explore.Active
function M.render(active)
  v.nvim_buf_clear_namespace(active.scratch.buf, tags_ns, 0, -1)

  local mode = active.display_mode
  if mode == "off" then return end

  render_target_highlight(active)
  if active.state.phase.kind ~= "Navigate" then return end

  if mode == "highlight" then
    render_landing_highlights(active)
  elseif mode == "inplace" then
    render_landing_highlights(active)
    render_tags_inplace(active)
  elseif mode == "below" then
    local placed = render_tags_virt_lines(active, false)
    render_landing_highlights(active, placed)
  else  -- "above" (default)
    local placed = render_tags_virt_lines(active, true)
    render_landing_highlights(active, placed)
  end
end

return M
