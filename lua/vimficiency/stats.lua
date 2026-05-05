-- `:Vimfy stats` — lifetime counters + efficiency + favorite motions.
--
-- Reads the append-only JSONL log from stats.log.read_all(), reduces
-- on-demand. No aggregate cache in v1 — the log is the source of truth;
-- add a cache only when stats-open time becomes a felt problem.

local v = vim.api
local log = require("vimficiency.stats.log")
local ffi_lib = require("vimficiency.ffi")
local util = require("vimficiency.util")

local M = {}

--- Minimum combined (user + optimizer) occurrences before a token
--- appears in the ratio table. Below this, the ratio is dominated by
--- noise. Raised from the early draft's 3 after we saw how sparse the
--- tail is in real logs.
local MIN_TOKEN_OCCURRENCES = 10

local MOTION_TOP_N = 8

local SPARK_LEVELS = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" }

-- 30-day efficiency chart dimensions. 4 rows × 8 SPARK_LEVELS = 32 levels
-- of granularity across 0..100, vs. the old single-row sparkline's 8.
local CHART_DAYS = 30
local CHART_ROWS = 4
-- Right-aligned y-axis tick labels (top row to row above baseline).
local CHART_Y_LABELS = { "100", " 75", " 50", " 25" }

-- One visible prefix before chart columns. Keeps the content indent
-- (4 spaces) consistent with sibling sections ("Score:", "Sessions:"),
-- then adds "NNN ┤" for the axis tick. Baseline uses "  0 └"+"─"×N.
local CHART_PREFIX_WIDTH = 4 + 3 + 2  -- "    " + "100" + " ┤"

--------------------------------------------------------------------------------
-- Token normalization
--------------------------------------------------------------------------------

--- Collapse tokens where the trailing character is purely arbitrary (a
--- target/replacement, not semantic content). Counts are preserved.
---
---   fa, Fb, tc, Td, re, Rf   → f_, F_, t_, T_, r_, R_
---   2fa                      → 2f_
---
--- IMPORTANT: add grouping here only when a strong empirical trend in the
--- data itself justifies it — not for aesthetic tidiness. Marks, registers,
--- operator+motion pairs (`dw` vs `d$`), counts on motions, etc. are all
--- deliberately left distinct. Grouping hides signal; we want the opposite
--- until we know which signals are noise.
function M.normalize_token(text)
  local count, op, tail = text:match("^(%d*)([fFtTrR])(.+)$")
  if op and tail and (#tail == 1 or tail:match("^<[^>]+>$")) then
    return count .. op .. "_"
  end
  return text
end

--------------------------------------------------------------------------------
-- Aggregation
--------------------------------------------------------------------------------

local function safe_tokenize(seq)
  if not seq or seq == "" then return {} end
  local ok, tokens = pcall(ffi_lib.tokenize_sequence, seq)
  if not ok or not tokens then return {} end
  return tokens
end

--- Reduce a list of log records into the summary the UI renders.
---@param records table[]
---@return table
function M.aggregate(records)
  local summary = {
    total_sessions = 0,
    by_type = { mark = 0, watch = 0, recall = 0, suggest = 0 },
    total_keys = 0,
    efficiency_score = nil,            -- 0..100, nil if no scorable records
    efficiency_by_day = {},            -- epoch_day (floor(sec/86400)) -> score
    motion_ratios = {},                -- text -> { user_count, opt_count, ratio }
    beats_count = 0,
    user_token_total = 0,
    opt_token_total = 0,
  }
  if #records == 0 then return summary end

  -- Geometric mean = exp(mean(log(x))). Accumulate log-sums.
  local log_sum, log_count = 0, 0
  local day_log_sum, day_count = {}, {}

  local user_hist, opt_hist = {}, {}
  local user_total, opt_total = 0, 0

  for _, rec in ipairs(records) do
    summary.total_sessions = summary.total_sessions + 1
    if rec.type and summary.by_type[rec.type] ~= nil then
      summary.by_type[rec.type] = summary.by_type[rec.type] + 1
    end
    summary.total_keys = summary.total_keys + (rec.key_count or 0)
    if rec.beats then summary.beats_count = summary.beats_count + 1 end

    -- Efficiency: capped ratio × 100. Beats clamp at 100 so an optimizer
    -- miss doesn't inflate the lifetime score past "as good as optimal".
    local uc = rec.user_cost
    local oc = rec.best_opt_cost
    if uc and oc and uc > 0 then
      local ratio = oc / uc
      if ratio > 1 then ratio = 1 end
      local score = ratio * 100
      if score > 0 then
        local l = math.log(score)
        log_sum = log_sum + l
        log_count = log_count + 1
        if rec.finish_epoch then
          local day = math.floor(rec.finish_epoch / 86400)
          day_log_sum[day] = (day_log_sum[day] or 0) + l
          day_count[day] = (day_count[day] or 0) + 1
        end
      end
    end

    -- Motion histograms (user & optimizer side-by-side).
    for _, tok in ipairs(safe_tokenize(rec.user_seq or "")) do
      local key = M.normalize_token(tok.text)
      user_hist[key] = (user_hist[key] or 0) + 1
      user_total = user_total + 1
    end
    for _, tok in ipairs(safe_tokenize(rec.best_opt_seq or "")) do
      local key = M.normalize_token(tok.text)
      opt_hist[key] = (opt_hist[key] or 0) + 1
      opt_total = opt_total + 1
    end
  end

  if log_count > 0 then
    summary.efficiency_score = math.exp(log_sum / log_count)
  end
  for day, sum in pairs(day_log_sum) do
    summary.efficiency_by_day[day] = math.exp(sum / day_count[day])
  end

  -- Per-token ratio user_prop / opt_prop, filtered by MIN_OCCURRENCES.
  local seen = {}
  for k in pairs(user_hist) do seen[k] = true end
  for k in pairs(opt_hist)  do seen[k] = true end
  for k in pairs(seen) do
    local uc2 = user_hist[k] or 0
    local oc2 = opt_hist[k]  or 0
    if uc2 + oc2 >= MIN_TOKEN_OCCURRENCES then
      local up = user_total > 0 and (uc2 / user_total) or 0
      local op = opt_total  > 0 and (oc2 / opt_total)  or 0
      local ratio
      if op > 0 then
        ratio = up / op
      elseif up > 0 then
        ratio = math.huge          -- user uses it; optimizer never does
      else
        ratio = 0
      end
      summary.motion_ratios[k] = {
        user_count = uc2, opt_count = oc2,
        user_prop = up,   opt_prop = op,
        ratio = ratio,
      }
    end
  end
  summary.user_token_total = user_total
  summary.opt_token_total = opt_total

  return summary
end

--------------------------------------------------------------------------------
-- Rendering
--------------------------------------------------------------------------------

local function format_score(score)
  if not score then return "—" end
  return string.format("%.1f / 100", score)
end

---Cell character for day `score` at a given row when stacking CHART_ROWS
---rows. `row_from_bottom` is 0 for the baseline row, CHART_ROWS-1 for top.
---Total eighths of height = round(score * CHART_ROWS * 8 / 100); this row
---fills up to 8 of those, spillover goes to the row above.
local function chart_cell(score, row_from_bottom)
  if not score then return " " end
  local total_eighths = math.floor((score * CHART_ROWS * 8 / 100) + 0.5)
  local row_eighths = total_eighths - row_from_bottom * 8
  if row_eighths <= 0 then return " " end
  if row_eighths >= 8 then return "█" end
  return SPARK_LEVELS[row_eighths]
end

---@param summary table
---@return string[]  lines — CHART_ROWS data lines + baseline + x-axis label
local function render_30day_chart(summary)
  local today = math.floor(os.time() / 86400)
  local out = {}
  for i = 1, CHART_ROWS do
    local row_from_bottom = CHART_ROWS - i
    local cells = {}
    for d = CHART_DAYS - 1, 0, -1 do
      cells[#cells + 1] = chart_cell(summary.efficiency_by_day[today - d], row_from_bottom)
    end
    out[#out + 1] = "    " .. CHART_Y_LABELS[i] .. " ┤" .. table.concat(cells, "")
  end
  out[#out + 1] = "      0 └" .. string.rep("─", CHART_DAYS)
  local left, right = "30 days ago", "today"
  local pad = CHART_DAYS - #left - #right
  if pad < 1 then pad = 1 end
  out[#out + 1] = string.rep(" ", CHART_PREFIX_WIDTH) .. left ..
    string.rep(" ", pad) .. right
  return out
end

local function format_duration(total_keys)
  -- Rough "time in tracked sessions" proxy — we don't store elapsed time
  -- per record in v1. Left as a count for now.
  return string.format("%d keys", total_keys)
end

--- Build the ranked motion lists from summary.motion_ratios. Returns
--- (over, under) lists sorted by how far the ratio is from 1. A small
--- deadband around 1 (±5%) suppresses the "everyone uses `w` about as
--- much as the optimizer" noise.
local function split_motion_lists(summary)
  local over, under = {}, {}
  for text, s in pairs(summary.motion_ratios) do
    if s.ratio == math.huge or s.ratio > 1.05 then
      over[#over + 1] = { text = text, ratio = s.ratio,
                          uc = s.user_count, oc = s.opt_count }
    elseif s.ratio < 0.95 then
      under[#under + 1] = { text = text, ratio = s.ratio,
                            uc = s.user_count, oc = s.opt_count }
    end
  end
  -- Over: highest ratio first (∞ sorts to top).
  table.sort(over, function(a, b)
    if a.ratio == b.ratio then return a.text < b.text end
    return a.ratio > b.ratio
  end)
  -- Under: lowest ratio first.
  table.sort(under, function(a, b)
    if a.ratio == b.ratio then return a.text < b.text end
    return a.ratio < b.ratio
  end)
  return over, under
end

local function format_ratio(r)
  if r == math.huge then return "  ∞×" end
  return string.format("%5.2f×", r)
end

---@param summary table
---@return string[] lines
---@return integer[] title_rows  0-indexed rows that should render as section titles
function M.build_lines(summary)
  local L = {}
  local title_rows = {}
  local function push(s) L[#L + 1] = s end
  local function push_title(s)
    push(s)
    title_rows[#title_rows + 1] = #L - 1
  end

  push("")

  -- Lifetime
  push_title("  Lifetime")
  local bt = summary.by_type
  push(string.format("    Sessions: %d  (mark %d · watch %d · recall %d · suggest %d)",
    summary.total_sessions, bt.mark, bt.watch, bt.recall, bt.suggest))
  push(string.format("    Keys:     %s", format_duration(summary.total_keys)))
  push("")

  -- Efficiency
  push_title("  Efficiency")
  push(string.format("    Score:    %s", format_score(summary.efficiency_score)))
  push("    30-day efficiency:")
  for _, line in ipairs(render_30day_chart(summary)) do
    push(line)
  end
  push("")

  -- Motions
  local over, under = split_motion_lists(summary)
  push_title("  Motions — you use MORE than the optimizer")
  if #over == 0 then
    push("    (nothing meets the ≥" .. MIN_TOKEN_OCCURRENCES ..
      "-occurrence threshold yet)")
  else
    for i = 1, math.min(#over, MOTION_TOP_N) do
      local e = over[i]
      push(string.format("    %-10s %s   (%d vs %d)",
        e.text, format_ratio(e.ratio), e.uc, e.oc))
    end
  end
  push("")
  push_title("  Motions — you use LESS than the optimizer")
  if #under == 0 then
    push("    (nothing meets the threshold yet)")
  else
    for i = 1, math.min(#under, MOTION_TOP_N) do
      local e = under[i]
      push(string.format("    %-10s %s   (%d vs %d)",
        e.text, format_ratio(e.ratio), e.uc, e.oc))
    end
  end
  push("")

  -- Dev
  push_title("  Dev")
  push(string.format("    Optimizer beats queued: %d", summary.beats_count))
  push(string.format("    Log: %s", vim.fn.fnamemodify(log._log_path(), ":~")))
  push("")
  push("  Press q / <Esc> to close.")
  push("")
  return L, title_rows
end

-- Bold+colored section titles. Links to Title so it picks up the active
-- colorscheme's heading style; users can override by defining the group
-- after our `default = true` install.
v.nvim_set_hl(0, "VimficiencyStatsSectionTitle",
  { link = "Title", default = true })
local stats_ns = v.nvim_create_namespace("vimfy_stats")

function M.open()
  local records = log.read_all()
  local summary = M.aggregate(records)
  local lines, title_rows = M.build_lines(summary)

  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency_stats"
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  for _, row in ipairs(title_rows) do
    v.nvim_buf_set_extmark(buf, stats_ns, row, 0, {
      end_row = row + 1, end_col = 0,
      hl_group = "VimficiencyStatsSectionTitle",
      hl_eol = true,
    })
  end
  vim.bo[buf].modifiable = false

  local width = 72
  local height = math.min(#lines, vim.o.lines - 4)
  local row = math.floor((vim.o.lines - height) / 2)
  local col = math.floor((vim.o.columns - width) / 2)
  local win = v.nvim_open_win(buf, true, {
    relative = "editor", row = row, col = col,
    width = width, height = height,
    border = "rounded", style = "minimal",
    title = " Vimfy Stats ", title_pos = "center",
  })
  util.configure_scratch_window(win)

  local function close()
    pcall(v.nvim_win_close, win, true)
  end
  vim.keymap.set("n", "q",     close, { buffer = buf, nowait = true, silent = true })
  vim.keymap.set("n", "<Esc>", close, { buffer = buf, nowait = true, silent = true })
end

return M
