local v = vim.api
local highlights = require("vimficiency.explore.highlights")
local sequence_display = require("vimficiency.sequence_display")

local M = {}

-- Shared with render/tags.lua — same namespace name resolves to the same ID.
local tags_ns = v.nvim_create_namespace("vimfy_explore_tags")

local METRIC_LABELS = {
  effort = "Effort",
  distance = "Distance",
  score = "Score",
}

local METRIC_FIELDS = {
  effort = "cost_diff",
  distance = "distance",
  score = "score",
}

local function recommendation_metric(active, item)
  local mode = active.recommendation_sort or "effort"
  local field = METRIC_FIELDS[mode] or "cost_diff"
  return item[field] or item.cost_diff or 0
end

---@class VF.Explore.RecRow
---@field rank string
---@field chunks string[]
---@field widths integer[] # target width per `chunks` slot (display columns)
---@field hl string

-- Align rendered columns, not UTF-8 byte counts.
local strwidth = vim.fn.strdisplaywidth

---@param row VF.Explore.RecRow
---@return string
local function format_rec_row(row)
  local parts = { row.rank }
  for i, chunk in ipairs(row.chunks) do
    local w = row.widths[i] or strwidth(chunk)
    local pad = math.max(0, w - strwidth(chunk))
    local sep = (i == 1) and " " or "  "
    parts[#parts + 1] = sep .. chunk .. string.rep(" ", pad)
  end
  return table.concat(parts)
end

---Render the recommendation list buffer.
---@param active VF.Explore.Active
---@param continuation VF.Explore.InsertContinuation
function M.render(active, continuation)
  if not v.nvim_buf_is_valid(active.list_buf) then return end

  local mode = active.recommendation_sort or "effort"
  local label = METRIC_LABELS[mode] or METRIC_LABELS.effort
  local lines = { "Recommendations (" .. label .. ")", "" }
  local hl_entries = {}

  ---@type VF.Explore.RecRow[]
  local rows = {}

  local function add_row(rank_index, total_ranks, chunks, hl)
    local rank_digits = math.max(1, #tostring(total_ranks))
    local rank = string.format("%" .. rank_digits .. "d.", rank_index)
    rows[#rows + 1] = { rank = rank, chunks = chunks, widths = {}, hl = hl }
  end

  local phase_kind = active.state.phase.kind
  if active.warning then
    local function warn_line(text)
      lines[#lines + 1] = text
      hl_entries[#hl_entries + 1] = {
        row = #lines - 1,
        hl = "DiagnosticError",
        end_col = #text,
      }
    end
    warn_line("!!! EXPLORE STATE WARNING !!!")
    warn_line(active.warning.title)
    for _, line in ipairs(active.warning.lines) do
      warn_line(line)
    end
  elseif active.state.is_completed then
    lines[#lines + 1] = "Session complete."
    lines[#lines + 1] = "u to undo  •  q to close"
  elseif phase_kind == "Insert" then
    local text = continuation.is_complete
      and "<Esc>"
      or sequence_display.typed_chunks_inline(continuation.chunks)
    add_row(1, 1, { text, "type" }, highlights.rank_hl(1))
  elseif #active.recommendations == 0 then
    lines[#lines + 1] = "(none)"
  else
    local is_motion = phase_kind == "Navigate"
    local total = #active.recommendations
    for i, item in ipairs(active.recommendations) do
      local text = sequence_display.inline(item.text)
      local cost = string.format("%.2f", recommendation_metric(active, item))
      local kind_label = is_motion and "move" or "edit"
      local chunks = { text, cost, kind_label }
      if is_motion then
        chunks[#chunks + 1] = string.format("-> (%d,%d)",
          item.landing.row, item.landing.col)
      end
      add_row(i, total, chunks, highlights.rank_hl(i))
    end
  end

  local col_widths = {}
  for _, row in ipairs(rows) do
    for i, chunk in ipairs(row.chunks) do
      col_widths[i] = math.max(col_widths[i] or 0, strwidth(chunk))
    end
  end
  for _, row in ipairs(rows) do
    for i = 1, #row.chunks do
      row.widths[i] = col_widths[i] or strwidth(row.chunks[i])
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
    v.nvim_buf_set_extmark(active.list_buf, tags_ns, entry.row, 0, {
      end_col = entry.end_col,
      hl_group = entry.hl,
      priority = 2100,
    })
  end
end

return M
