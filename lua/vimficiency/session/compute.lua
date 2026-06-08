local v = vim.api

local config = require("vimficiency.config")
local disk = require("vimficiency.session.disk")
local ffi_optimizer = require("vimficiency.ffi.optimizer")
local ffi_session = require("vimficiency.ffi.session")
local key_tracking = require("vimficiency.capture.key_tracking")
local keynorm = require("vimficiency.capture.keynorm")
local util = require("vimficiency.util")

local M = {}

local APPROXIMATE_MOTION_CONVERSIONS = {
  ["gj"] = "j",
  ["gk"] = "k",
}

-- Diagnostic probe for the synchronous optimizer block. During a fire nothing
-- can render, so a slow run gives no in-the-moment feedback. On any fire over
-- the threshold, dump the exact slice for offline replay and surface the timing
-- the instant the main thread frees. Tune/disable via vim.g.vimficiency_slow_fire_ms.
local SLOW_FIRE_DEFAULT_MS = 500

local function slow_fire_threshold_ms()
  return tonumber(vim.g.vimficiency_slow_fire_ms) or SLOW_FIRE_DEFAULT_MS
end

---@param active VF.Session.Active
---@param result VF.Session.Result
local function log_slow_fire(active, result)
  pcall(function()
    local dir = vim.fn.stdpath("data") .. "/vimficiency/slow"
    vim.fn.mkdir(dir, "p")
    local path = string.format("%s/%d-%s.json", dir, os.time(), tostring(active.id))
    vim.fn.writefile({ vim.json.encode(result) }, path)
    vim.schedule(function()
      vim.notify(string.format(
        "vimfy: optimizer ran %.0fms (analyze %.0fms) on a %d-line slice — repro: %s",
        result.compute_ms or 0, result.analyze_ms or 0, #result.lines,
        vim.fn.fnamemodify(path, ":~")), vim.log.levels.WARN)
    end)
  end)
end

---@param keyseq string
---@return string
local function apply_motion_conversions(keyseq)
  local result = keyseq
  for from, to in pairs(APPROXIMATE_MOTION_CONVERSIONS) do
    result = result:gsub(vim.pesc(from), to)
  end
  return result
end

---@param key_seq VF.KeyEvent[]|nil
---@param reduced_sequence string
---@param normalized_sequence string
---@return table
local function build_capture_debug(key_seq, reduced_sequence, normalized_sequence)
  key_seq = key_seq or {}
  local events = {}
  local raw_parts = {}
  local first_t = key_seq[1] and tonumber(key_seq[1].t) or nil

  for i, ev in ipairs(key_seq) do
    raw_parts[#raw_parts + 1] = ev.key_typed or ""
    local t = tonumber(ev.t)
    events[#events + 1] = {
      index = i,
      t = tostring(ev.t or ""),
      dt_ms = first_t and t and ((t - first_t) / 1000000) or 0,
      mode = ev.mode or "",
      key_sent = ev.key_sent or "",
      key_sent_raw = ev.key_sent_raw or "",
      key_typed = ev.key_typed or "",
      key_typed_raw = ev.key_typed_raw or "",
      win = ev.win or 0,
      buf = ev.buf or 0,
    }
  end

  return {
    version = 1,
    event_count = #key_seq,
    raw_joined = table.concat(raw_parts),
    reduced_sequence = reduced_sequence,
    normalized_sequence = normalized_sequence,
    events = #events > 0 and events or disk.empty_array(),
  }
end

---@param buf integer
---@return string
local function buf_display_name(buf)
  if not v.nvim_buf_is_valid(buf) then
    return "<invalid buffer " .. tostring(buf) .. ">"
  end
  local name = v.nvim_buf_get_name(buf)
  if name == "" then name = "[No Name]" end
  local buftype = vim.bo[buf].buftype
  local suffix = ""
  if buftype ~= "" then
    suffix = " [" .. buftype .. "]"
  end
  return string.format("'%s' (buf %d)%s", name, buf, suffix)
end

---@param lines string[]
---@param region_start integer
---@param region_end integer
---@return string[]
local function slice_lines(lines, region_start, region_end)
  local sliced = {}
  for i = region_start + 1, math.min(region_end + 1, #lines) do
    table.insert(sliced, lines[i])
  end
  return sliced
end

--- Drop mouse events from the captured stream. They are uncapturable as
--- keyboard motion and otherwise inflate cost/length wildly (each unknown
--- `<…>` token is costed character-by-character). `had_mouse` drives a warning.
---@param key_seq VF.KeyEvent[]|nil
---@return VF.KeyEvent[] kept
---@return boolean had_mouse
local function filter_mouse_events(key_seq)
  local kept, had_mouse = {}, false
  for _, ev in ipairs(key_seq or {}) do
    if keynorm.is_mouse(ev.key_typed) then
      had_mouse = true
    else
      kept[#kept + 1] = ev
    end
  end
  return kept, had_mouse
end

---@param active VF.Session.Active
---@return VF.Session.Result|nil result
---@return string|nil err
function M.compute_result_for_active(active)
  local compute_t0 = vim.uv.hrtime()
  if not v.nvim_buf_is_valid(active.buf) then
    return nil, string.format(
      "original buffer %s is no longer valid",
      buf_display_name(active.buf))
  end

  local curr_buf = v.nvim_get_current_buf()
  if curr_buf ~= active.buf then
    return nil, string.format(
      "session captured in %s, but you're now in %s. " ..
      "For recall, this usually means one of the recent keystrokes fired " ..
      "while a different buffer was focused (scratch/result window from a " ..
      "prior :Vimfy command, help buffer, popup, or a <C-^>/<C-w>w switch). " ..
      "Focus the captured buffer and re-run, or use a smaller window.",
      buf_display_name(active.buf),
      buf_display_name(curr_buf))
  end

  local win = active.win
  if not v.nvim_win_is_valid(win) or v.nvim_win_get_buf(win) ~= active.buf then
    win = v.nvim_get_current_win()
  end

  local start_state = assert(active.start_state,
    "compute_result_for_active: live session must carry a start_state (only fetched/imported records may have nil)")
  local end_state = util.capture_state(active.buf, win)

  util.check_state_inconsistencies(start_state, end_state)

  local initial_lines = start_state.lines
  local goal_lines = end_state.lines

  local start_search, end_search = util.compute_search_region(
    start_state.row, end_state.row,
    initial_lines, goal_lines,
    config.SLICE_PADDING
  )

  if end_search - start_search > config.MAX_SEARCH_LINES then
    return nil, "search range exceeds MAX_SEARCH_LINES"
  end

  local initial_slice = slice_lines(initial_lines, start_search, end_search)
  local goal_slice = slice_lines(goal_lines, start_search, end_search)

  local kept_events, had_mouse = filter_mouse_events(active.key_seq)
  local keyseq_raw = key_tracking.build_sequence(kept_events)
  local keyseq_str = apply_motion_conversions(keyseq_raw)

  local rel_start_row = start_state.row - start_search
  local rel_start_col = start_state.col
  local rel_end_row = end_state.row - start_search
  local rel_end_col = end_state.col

  local boundary_first_col = 0
  local boundary_last_col = #initial_slice[#initial_slice] - 1
  if boundary_last_col < 0 then boundary_last_col = 0 end
  local has_lines_above = start_search > 0
  local has_lines_below = end_search < math.max(#initial_lines, #goal_lines) - 1

  local optimizer_overrides = ffi_optimizer.encode_optimizer_overrides({ shared = config.optimizer })

  local analyze_t0 = vim.uv.hrtime()
  local ok, results, user_cost, dbg = pcall(
    ffi_optimizer.analyze,
    initial_slice, goal_slice,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    rel_start_row,
    rel_start_col,
    rel_end_row,
    rel_end_col,
    keyseq_str,
    start_state.window_height,
    start_state.scroll_amount,
    config.RESULTS_CALCULATED,
    optimizer_overrides
  )
  local analyze_ms = (vim.uv.hrtime() - analyze_t0) / 1e6

  if not ok then
    return nil, "FFI error: " .. tostring(results)
  end

  if dbg and dbg ~= "" then
    pcall(function()
      local debug_dir = vim.fn.stdpath("data") .. "/vimficiency/debug"
      vim.fn.mkdir(debug_dir, "p")
      local debug_path = debug_dir .. "/" .. active.id .. ".txt"
      vim.fn.writefile(vim.split(dbg, "\n"), debug_path)
    end)
  end

  local optimal_results = {}
  for i = 1, math.min(#results, config.RESULTS_SAVED) do
    table.insert(optimal_results, results[i])
  end
  if #optimal_results == 0 then
    optimal_results = disk.empty_array()
  end

  -- The optimizer's own character-level diff, for column highlighting in views.
  -- Pass the same composition diff settings analyze used so the stored diff is
  -- the breakdown the optimizer planned against. Same validated slices, so it
  -- can't fail here.
  local comp = (config.optimizer or {}).composition or {}
  local diff_open_penalty = comp.diffOpenPenalty or comp.treeDiffOpenPenalty
  local diffs = ffi_optimizer.compute_diffs(
    initial_slice, goal_slice, comp.diffAlgorithm, diff_open_penalty)
  if #diffs == 0 then
    diffs = disk.empty_array()
  end

  local compute_ms = (vim.uv.hrtime() - compute_t0) / 1e6
  local result = {
    lines = initial_slice,
    goal_lines = goal_slice,
    start_row = rel_start_row,
    start_col = rel_start_col,
    end_row = rel_end_row,
    end_col = rel_end_col,
    has_lines_above = has_lines_above,
    has_lines_below = has_lines_below,
    -- Reserved boundary context. At the whole-buffer level the slice IS the
    -- edit region (boundary_first_col=0, boundary_last_col=last), so horizontal
    -- prefix/suffix are always empty; nonempty values only arise per planned
    -- edit inside CompositionOptimizer. Views render them faded when present.
    prefix = "",
    suffix = "",
    diffs = diffs,
    window_height = start_state.window_height,
    scroll_amount = start_state.scroll_amount,
    user_seq = keyseq_str,
    user_cost = user_cost,
    had_mouse = had_mouse,
    optimal_results = optimal_results,
    capture_debug = build_capture_debug(active.key_seq, keyseq_raw, keyseq_str),
    start_time = active.time_started,
    key_count = #kept_events,
    timestamp = vim.uv.hrtime(),
    analyze_ms = analyze_ms,
    compute_ms = compute_ms,
  }
  if compute_ms >= slow_fire_threshold_ms() then
    log_slow_fire(active, result)
  end
  return result, nil
end

---@param session VF.Session.Active
---@param cursor_row integer
---@param now_ns integer
---@return string|nil
function M.manual_should_evict(session, cursor_row, now_ns)
  local seq = session.key_seq
  local last_key_time_ns = (seq and #seq > 0) and seq[#seq].t or nil
  return ffi_session.manual_evict_reason(
    session.start_state.row,
    cursor_row,
    last_key_time_ns,
    now_ns,
    config.MAX_SEARCH_LINES,
    config.MANUAL_IDLE_TIMEOUT_SECONDS
  )
end

return M
