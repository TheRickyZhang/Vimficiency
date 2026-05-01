-- Recommendation list view for the left-hand explore panel.
--
-- Two modes, dispatched off `active.state.phase.kind`:
--   Insert    — one row: `1. <remaining-or-<Esc>>  type`, shrinks live.
--   Otherwise — rank-ordered table of motion/edit recs with cost, kind,
--               and landing coords, columns aligned.
--
-- Callers precompute `remaining` (the insert tail) so the panel
-- stays in lockstep with the header within one frame.
local v = vim.api
local highlights = require("vimficiency.explore.highlights")
local sequence_display = require("vimficiency.sequence_display")

local M = {}

-- Shared with render/tags.lua — same namespace name resolves to the same ID.
local tags_ns = v.nvim_create_namespace("vimfy_explore_tags")

---@class VF.Explore.RecRow
---@field rank string   # e.g. " 1."  — rank marker, already padded
---@field chunks string[]  # remaining columns in order, no padding applied yet
---@field widths integer[] # target width per `chunks` slot (bytes)
---@field hl string       # rank hl group name

---Join one row into its final padded string. One space after the rank
---marker (matches existing density), two spaces between other columns.
---@param row VF.Explore.RecRow
---@return string
local function format_rec_row(row)
  local parts = { row.rank }
  for i, chunk in ipairs(row.chunks) do
    local w = row.widths[i] or #chunk
    local pad = math.max(0, w - #chunk)
    local sep = (i == 1) and " " or "  "
    parts[#parts + 1] = sep .. chunk .. string.rep(" ", pad)
  end
  return table.concat(parts)
end

---Render the recommendation list buffer.
---@param active VF.Explore.Active
---@param remaining string  live pending-insert tail (empty in other phases)
function M.render(active, remaining)
  if not v.nvim_buf_is_valid(active.list_buf) then return end

  local lines = { "Recommendations", "" }
  local hl_entries = {}

  ---@type VF.Explore.RecRow[]
  local rows = {}

  local function add_row(rank_index, total_ranks, chunks, hl)
    local rank_digits = math.max(1, #tostring(total_ranks))
    local rank = string.format("%" .. rank_digits .. "d.", rank_index)
    rows[#rows + 1] = { rank = rank, chunks = chunks, widths = {}, hl = hl }
  end

  if active.state.phase.kind == "Insert" then
    -- Parallel to the motion format: rank, text, kind. When the insert is
    -- complete the text slot shows `<Esc>` so there's still a concrete
    -- action to perform before phase advancement.
    local text = remaining == "" and "<Esc>" or sequence_display.inline(remaining)
    add_row(1, 1, { text, "type" }, highlights.rank_hl(1))
  elseif #active.recommendations == 0 then
    lines[#lines + 1] = "(none)"
  else
    local total = #active.recommendations
    for i, item in ipairs(active.recommendations) do
      local text = sequence_display.inline(item.text)
      local cost = string.format("%.2f", item.total_path_cost)
      local kind = item.kind == "movement" and "move" or item.kind
      local chunks = { text, cost, kind }
      if item.kind == "movement" then
        chunks[#chunks + 1] = string.format("-> (%d,%d)",
          item.landing.row, item.landing.col)
      end
      add_row(i, total, chunks, highlights.rank_hl(i))
    end
  end

  -- Column-width pass: widest chunk per slot across every staged row.
  local col_widths = {}
  for _, row in ipairs(rows) do
    for i, chunk in ipairs(row.chunks) do
      col_widths[i] = math.max(col_widths[i] or 0, #chunk)
    end
  end
  for _, row in ipairs(rows) do
    for i = 1, #row.chunks do
      row.widths[i] = col_widths[i] or #row.chunks[i]
    end
    lines[#lines + 1] = format_rec_row(row)
    hl_entries[#hl_entries + 1] = {
      row = #lines - 1,
      hl = row.hl,
      end_col = #row.rank,
    }
  end

  vim.bo[active.list_buf].modifiable = true
  v.nvim_buf_set_lines(active.list_buf, 0, -1, false, lines)
  vim.bo[active.list_buf].modifiable = false

  v.nvim_buf_clear_namespace(active.list_buf, tags_ns, 0, -1)
  for _, entry in ipairs(hl_entries) do
    -- Highlight only the leading rank marker so colors don't wash the whole line.
    v.nvim_buf_set_extmark(active.list_buf, tags_ns, entry.row, 0, {
      end_col = entry.end_col,
      hl_group = entry.hl,
      priority = 2100,
    })
  end
end

return M
