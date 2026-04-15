local v = vim.api
local alias_mod = require("vimficiency.alias")
local config = require("vimficiency.config")
local end_trigger = require("vimficiency.end_trigger")
local util = require("vimficiency.util")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.key_tracking")
local ffi_lib = require("vimficiency.ffi")
local session_store = require("vimficiency.session_store")
local result_view = require("vimficiency.result_view")

local M = {}

-------- Local functions BEGIN --------

-- Approximate motion conversions: screen-line motions -> buffer-line equivalents
-- These are NOT exact (gj/gk work on display lines, j/k on buffer lines) but
-- are close enough for optimization comparison purposes.
local APPROXIMATE_MOTION_CONVERSIONS = {
  ["gj"] = "j",
  ["gk"] = "k",
}

--- Apply approximate motion conversions to a key sequence string
---@param keyseq string
---@return string
local function apply_motion_conversions(keyseq)
  local result = keyseq
  for from, to in pairs(APPROXIMATE_MOTION_CONVERSIONS) do
    result = result:gsub(vim.pesc(from), to)
  end
  return result
end

---@param record SessionRecord   Session being finished (must have id + start_kind).
---@param title string
---@param text string
---@param notify_message string|nil
---@param level integer|nil
local function total_failure(record, title, text, notify_message, level)
  util.show_output(title, text)
  -- Manual sessions (Mark/Watch) end here — remove the record so the
  -- alias is free and per-session key tracking is detached. Recall and
  -- Suggest records live in the rolling ring and represent a slice of
  -- history; a failed finish (wrong buffer, search range too large,
  -- optimizer error) must NOT destroy that slice, or a later retry over
  -- the same window would silently find nothing. Ring capacity/age caps
  -- own their lifetime.
  if record.start_kind == "manual" then
    session_store.remove(record.id)
  end
  if notify_message or title then
    vim.schedule(function()
      vim.notify(notify_message or title, level or vim.log.levels.ERROR)
    end)
  end
end

--- Get the save directory path
---@return string
local function get_save_dir()
  return vim.fn.stdpath("data") .. "/vimficiency/saved"
end

--- Save results to JSON file (pretty-printed for readability)
---@param name string
---@param data table
---@return boolean success
---@return string|nil error
local function save_results(name, data)
  local save_dir = get_save_dir()
  vim.fn.mkdir(save_dir, "p")
  local path = save_dir .. "/" .. name .. ".json"
  -- Pretty print with 2-space indent
  local json = vim.json.encode(data, { indent = true })
  -- vim.json.encode with indent returns a single string with newlines
  local lines = vim.split(json, "\n")
  local ok, err = pcall(vim.fn.writefile, lines, path)
  if not ok then
    return false, err
  end
  return true, nil
end

--- Load results from JSON file
---@param name string
---@return table|nil data
---@return string|nil error
local function load_results(name)
  local path = get_save_dir() .. "/" .. name .. ".json"
  if vim.fn.filereadable(path) == 0 then
    return nil, "File not found: " .. path
  end
  local lines = vim.fn.readfile(path)
  local json_str = table.concat(lines, "\n")
  local ok, data = pcall(vim.json.decode, json_str)
  if not ok then
    return nil, "Failed to parse JSON: " .. tostring(data)
  end
  return data, nil
end

--- Build a ResultSession from an active session by capturing current state
--- and running the optimizer. Returns nil + error string on failure.
--- Does NOT touch session_store; caller decides where to route the result.
---@param active ActiveSession
---@return ResultSession|nil result
---@return string|nil err
local function compute_result_for_active(active)
  if not v.nvim_buf_is_valid(active.buf) then
    return nil, "buffer no longer valid"
  end

  local curr_buf = v.nvim_get_current_buf()
  if curr_buf ~= active.buf then
    return nil, "not in original buffer"
  end

  -- Recall is retrospective; the original window may have been closed
  -- or switched to a different buffer between session start and `end`.
  -- Fall back to the current window, which we've just confirmed
  -- displays the original buffer.
  local win = active.win
  if not v.nvim_win_is_valid(win) or v.nvim_win_get_buf(win) ~= active.buf then
    win = v.nvim_get_current_win()
  end

  local start_state = active.start_state
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

  local function slice_lines(lines, region_start, region_end)
    local sliced = {}
    for i = region_start + 1, math.min(region_end + 1, #lines) do
      table.insert(sliced, lines[i])
    end
    return sliced
  end

  local initial_slice = slice_lines(initial_lines, start_search, end_search)
  local goal_slice = slice_lines(goal_lines, start_search, end_search)

  local keyseq_raw = key_tracking.build_sequence(active.key_seq)
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

  local ok, results, user_cost, dbg = pcall(
    ffi_lib.analyze,
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
    config.RESULTS_CALCULATED
  )

  if not ok then
    return nil, "FFI error: " .. tostring(results)
  end

  if dbg and dbg ~= "" then
    -- Best-effort: a read-only stdpath("data") must not break finish.
    pcall(function()
      local debug_dir = vim.fn.stdpath("data") .. "/vimficiency/debug"
      vim.fn.mkdir(debug_dir, "p")
      local debug_path = debug_dir .. "/" .. active.id .. ".txt"
      vim.fn.writefile(vim.split(dbg, "\n"), debug_path)
    end)
  end

  ---@type VimficiencyResult[]
  local optimal_results = {}
  for i = 1, math.min(#results, config.RESULTS_SAVED) do
    table.insert(optimal_results, results[i])
  end

  ---@type ResultSession
  local result = {
    lines = initial_slice,
    start_row = rel_start_row,
    start_col = rel_start_col,
    end_row = rel_end_row,
    end_col = rel_end_col,
    user_seq = keyseq_str,
    user_cost = user_cost,
    optimal_results = optimal_results,
    start_time = active.time_started,
    key_count = #active.key_seq,
    timestamp = vim.uv.hrtime(),
  }
  return result, nil
end

--- Public alias over the local compute_result_for_active, for modules that
--- share the "take an active session, run the optimizer, hand back a result"
--- path (currently auto_suggest). Keeps the notification/policy layer (who
--- displays what, when to stay quiet) in the caller.
---@param active ActiveSession
---@return ResultSession|nil, string|nil
function M.compute_result_for_active(active)
  return compute_result_for_active(active)
end

--- Decide whether an in-progress manual session should be auto-evicted.
--- Pure: reads only the passed arguments, no module state, no side effects.
---
--- Drift wins over idle when both trigger: a session that's both stale
--- AND drifted is more interesting to diagnose as drift (the cursor is
--- the direct reason the optimizer would fail).
---
---@param session ActiveSession  Must have start_state.row and (optionally) key_seq
---@param cursor_row integer     0-indexed current cursor row
---@param now_ns integer         vim.uv.hrtime() snapshot
---@return string|nil reason     Human-readable reason if evict, else nil
function M.manual_should_evict(session, cursor_row, now_ns)
  local seq = session.key_seq
  local last_key_time_ns = (seq and #seq > 0) and seq[#seq].t or nil
  return ffi_lib.manual_evict_reason(
    session.start_state.row,
    cursor_row,
    last_key_time_ns,
    now_ns,
    config.MAX_SEARCH_LINES,
    config.MANUAL_IDLE_TIMEOUT_SECONDS
  )
end

-------- Local functions END --------

---@param alias string  The alias for the session (required, must be manual type)
function M.start(alias)
  if not alias or alias == "" then
    vim.notify("start() requires a session alias", vim.log.levels.ERROR)
    return
  end

  -- Only manual (alphabetic) aliases can be started explicitly.
  -- Anything else — recall syntax, hyphens, digits, empty — is rejected.
  if not alias_mod.is_valid_manual(alias) then
    vim.notify(
      "Invalid session alias '" .. alias .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').",
      vim.log.levels.ERROR
    )
    return
  end

  -- Check capacity before allocating resources
  if not session_store.can_store_manual(alias) then
    vim.notify("Manual session capacity reached", vim.log.levels.ERROR)
    return
  end

  local buf = v.nvim_get_current_buf()
  local win = v.nvim_get_current_win()
  local id = util.new_id(buf)
  local start_state = util.capture_state(buf, win)

  -- `id` is allocated above and captured in the closures below. That
  -- keeps the detach/removal path keyed on the stable id even if the
  -- manual alias is later overwritten to point at a different session.
  local key_nsid = key_tracking.attach(
    function()
      return session_store.get_active(alias)
    end,
    function(reason, level) ---@param reason string @param level integer
      session_store.remove(id)
      if reason then
        vim.schedule(function()
          vim.notify(reason, level or vim.log.levels.INFO)
        end)
      end
    end,
    function(session)
      -- Window gone → session context is dead; evict. Without this guard
      -- nvim_win_get_cursor throws and breaks key tracking.
      if not v.nvim_win_is_valid(session.win) then
        return "Vimficiency: session [" .. alias .. "] dropped — window closed"
      end
      -- Cursor is [row1, col0]; start_state.row is 0-indexed.
      local cursor = v.nvim_win_get_cursor(session.win)
      local reason = M.manual_should_evict(
        session, cursor[1] - 1, vim.uv.hrtime()
      )
      if reason then
        return "Vimficiency: session [" .. alias .. "] dropped — " .. reason
      end
      return nil
    end
  )

  -- Mark: manual start, manual end.
  local active = session_store.new_active_session(
    id, key_nsid, win, buf, start_state, "manual", "manual")
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimficiency started [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimficiency started [" .. alias .. "]", vim.log.levels.INFO)
  end
end


--- Watch: manual start, auto end. Like M.start but arms an idle end
--- trigger instead of waiting for `:Vimfy end <alias>`. After
--- `config.watch.idle.ms` of real keystroke idleness (global, not
--- per-session), the trigger fires M.finish on this alias. Cooldown
--- between fires is enforced by end_trigger so a post-fire pause
--- doesn't immediately re-finish.
---@param alias string  The alias for the session (required, must be manual type)
function M.watch(alias)
  if not alias or alias == "" then
    vim.notify("watch() requires a session alias", vim.log.levels.ERROR)
    return
  end

  if not alias_mod.is_valid_manual(alias) then
    vim.notify(
      "Invalid session alias '" .. alias .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').",
      vim.log.levels.ERROR
    )
    return
  end

  local cfg = config.watch
  if not cfg then
    vim.notify(
      "Watch is not configured. Add `watch = { idle = { ms = N }, cooldown_ms = N }` to setup{}.",
      vim.log.levels.ERROR
    )
    return
  end

  if not session_store.can_store_manual(alias) then
    vim.notify("Manual session capacity reached", vim.log.levels.ERROR)
    return
  end

  local buf = v.nvim_get_current_buf()
  local win = v.nvim_get_current_win()
  local id = util.new_id(buf)
  local start_state = util.capture_state(buf, win)

  local key_nsid = key_tracking.attach(
    function()
      return session_store.get_active(alias)
    end,
    function(reason, level) ---@param reason string @param level integer
      session_store.remove(id)
      if reason then
        vim.schedule(function()
          vim.notify(reason, level or vim.log.levels.INFO)
        end)
      end
    end,
    function(session)
      if not v.nvim_win_is_valid(session.win) then
        return "Vimficiency: watch [" .. alias .. "] dropped — window closed"
      end
      local cursor = v.nvim_win_get_cursor(session.win)
      local reason = M.manual_should_evict(
        session, cursor[1] - 1, vim.uv.hrtime()
      )
      if reason then
        return "Vimficiency: watch [" .. alias .. "] dropped — " .. reason
      end
      return nil
    end
  )

  -- Watch: manual start, auto end.
  local active = session_store.new_active_session(
    id, key_nsid, win, buf, start_state, "manual", "auto")

  -- Arm the idle end-trigger. Name-scoped to this session's id so
  -- concurrent watches coexist (each owns its own global subscriber
  -- and timer).
  local disarm = end_trigger.arm_idle({
    name        = "watch_" .. id,
    idle_ms     = cfg.idle.ms,
    cooldown_ms = cfg.cooldown_ms,
    on_fire     = function()
      -- Re-resolve through the store so we notice if the alias has
      -- been rebound (overwrite by another watch/start) or the record
      -- has been destroyed. Bail without finishing in those cases —
      -- the replacement has its own trigger and cleanup runs through
      -- destroy_record's disarm.
      local rec = session_store.get_active(alias)
      if not rec or rec.id ~= id then return false end
      M.finish(alias, "watch_idle")
      return true
    end,
  })

  if not disarm then
    key_tracking.detach(key_nsid)
    vim.notify("vimficiency watch [" .. alias .. "] failed to arm idle trigger", vim.log.levels.ERROR)
    return
  end

  active.watch_disarm = disarm
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimficiency watching [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimficiency watching [" .. alias .. "]", vim.log.levels.INFO)
  end
end


---@param alias string  The alias of the session to finish
---@param reason FinishReason|nil  Why the finish was triggered. Defaults to "manual" (the `:Vimfy end` path). Watch's idle callback passes "watch_idle".
function M.finish(alias, reason)
  reason = reason or "manual"
  if not alias or alias == "" then
    vim.notify("finish() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    local session_type = alias_mod.parse(alias)
    if session_type == "recall_key" or session_type == "recall_time" then
      if not session_store.is_recall_enabled() then
        vim.notify("Recall not enabled. Run ':Vimfy recall on' first.", vim.log.levels.ERROR)
      elseif session_type == "recall_key" then
        vim.notify("No recall session found for '" .. alias .. "' keys ago.", vim.log.levels.ERROR)
      else
        vim.notify("No recall session found within '" .. alias .. "'. Try a larger window or 'end N' by key count.", vim.log.levels.ERROR)
      end
    elseif session_type == "manual" then
      vim.notify("Session '" .. alias .. "' not found or already finished.", vim.log.levels.ERROR)
    else
      vim.notify("Unrecognized alias '" .. alias .. "'. " ..
        "Expected alphabetic (manual), digits (recall keys), or digits+s (recall time).",
        vim.log.levels.ERROR)
    end
    return
  end

  -- Pin the resolved id here. Everything that mutates the store below
  -- must use this id, not `alias` — recall aliases (`3s`, `5`) drift as
  -- the ring rotates, so re-resolving could touch a different record
  -- than the one we just computed a result for.
  local id = active.id

  local result, err = compute_result_for_active(active)
  if not result then
    total_failure(active, "finish()", err or "unknown error")
    return
  end

  -- This detaches key tracking and moves from active to result storage.
  -- Pass the literal `alias` so `:Vimfy save @` can default the filename
  -- to what the user typed at end-time (otherwise recall positions drift).
  if not session_store.finish_session(id, result, alias, nil, reason) then
    total_failure(active, "finish()", "failed to store result")
    return
  end

  local header = "vimficiency finished [" .. alias .. "] "
    .. result_view.format_position(result)
    .. result_view.format_reason_suffix(result)
  local body = result_view.format_body(result)
  vim.notify(header .. "\n" .. table.concat(body, "\n"), vim.log.levels.INFO)
end

--- Default filename for `:Vimfy save <selector>` when the caller omits
--- a name. Every legal selector (`a`, `refactor`, `5`, `3s`) is also a
--- valid saved-file name, so we return it verbatim. For `@`, resolve to
--- the alias that was used at `:Vimfy end` time — stored on the record
--- because recall positions drift and can't be reconstructed later.
---
--- Returns nil only when `@` has no finished session to resolve to;
--- invalid selectors pass through and fail downstream in `M.save` with
--- the existing "no finished result" diagnostic.
---@param selector string
---@return string|nil
function M.default_save_name(selector)
  if selector == "@" then
    return session_store.get_last_finished_alias()
  end
  return selector
end

--- Save a finished session result to disk under `name`.
--- Selector can be any alias that resolves to a finished result, or `@`
--- for the most recently finished session. Saved results live in a
--- separate namespace from live session handles — see `:Vimfy view`.
---@param selector string
---@param name string
function M.save(selector, name)
  -- Saved names are filesystem fragments (see alias.is_valid_saved_name).
  -- Refuse anything that could escape the saved/ directory before we
  -- concat it into a path.
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  local result
  if selector == "@" then
    result = session_store.get_last_finished_result()
    if not result then
      vim.notify("No recently finished session to save. Run ':Vimfy end <alias>' first.", vim.log.levels.ERROR)
      return
    end
  else
    result = session_store.get_result(selector)
    if not result then
      vim.notify("No finished result for '" .. selector .. "'. Is the session still active?", vim.log.levels.ERROR)
      return
    end
  end

  local ok, err = save_results(name, result)
  if ok then
    vim.notify("vimficiency saved [" .. name .. "]", vim.log.levels.INFO)
  else
    vim.notify("vimficiency save failed: " .. (err or "unknown error"), vim.log.levels.ERROR)
  end
end

--- Close a session without finishing (no optimization, no result stored).
---@param alias string  The alias of the session to close
function M.close(alias)
  if not alias or alias == "" then
    vim.notify("close() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    vim.notify("Session '" .. alias .. "' not found or already closed", vim.log.levels.WARN)
    return
  end

  -- Pin id at entry; see `finish()` for the same reasoning.
  -- remove() handles detaching key tracking (which stops macro recording)
  session_store.remove(active.id)
  vim.notify("vimficiency closed [" .. alias .. "]", vim.log.levels.INFO)
end

--- Enable the rolling recall ring. Every keystroke creates a retained
--- session, queryable as `N` (keys ago) or `Ns` (seconds ago).
---@param opts? { quiet?: boolean }  Pass { quiet = true } to suppress the user notification (e.g., when another command — :Vimfy suggest on — is turning recall on as a side effect).
---@return boolean success
function M.enable_recall(opts)
  opts = opts or {}
  local success = session_store.enable_recall()
  if not opts.quiet then
    if success then
      vim.notify("vimficiency recall enabled", vim.log.levels.INFO)
    else
      vim.notify("vimficiency recall already enabled", vim.log.levels.WARN)
    end
  end
  return success
end

--- Disable the recall ring. Discards all retained sessions.
---
--- Auto-suggest depends on the recall ring — without it there's
--- nothing for the idle trigger to analyze. If auto-suggest is on when
--- recall is turned off, cascade the disable: leaving the idle timer
--- armed and the key subscriber attached with no ring to act on is
--- "surprising background work" (reviewer's phrasing). Uses a lazy
--- require to sidestep the session ↔ auto_suggest import cycle.
function M.disable_recall()
  local auto_suggest = require("vimficiency.auto_suggest")
  if auto_suggest.is_enabled() then
    auto_suggest.disable()
    vim.notify("vimficiency auto-suggest disabled (depends on recall)", vim.log.levels.INFO)
  end
  session_store.disable_recall()
  vim.notify("vimficiency recall disabled", vim.log.levels.INFO)
end

---@return boolean
function M.is_recall_enabled()
  return session_store.is_recall_enabled()
end


---@param alias string  The alias of the session to simulate
---@param count integer|nil  How many optimal results to show (default: all saved)
---@param delay_ms integer|nil  Delay between steps in ms (default 1000)
function M.simulate(alias, count, delay_ms)
  delay_ms = delay_ms or 1000

  if not alias or alias == "" then
    vim.notify("simulate() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local result = session_store.get_result(alias)
  if not result then
    vim.notify("No results for session '" .. alias .. "'. Run finish() first.", vim.log.levels.ERROR)
    return
  end

  -- Build sequences: user sequence + top N optimal results
  local sequences = {}

  -- Always include user sequence first (if different from best optimal)
  local user_seq = result.user_seq or ""
  local optimal_results = result.optimal_results or {}
  local first_optimal = optimal_results[1] and optimal_results[1].seq or ""

  if user_seq ~= "" and user_seq ~= first_optimal then
    table.insert(sequences, user_seq)
  end

  -- Add optimal sequences (limited by count if provided)
  local num_to_show = count or #optimal_results
  for i = 1, math.min(num_to_show, #optimal_results) do
    table.insert(sequences, optimal_results[i].seq)
  end

  if #sequences == 0 then
    vim.notify("No sequences to simulate", vim.log.levels.WARN)
    return
  end

  simulate.simulate_compare(
    result.lines,
    result.start_row,
    result.start_col,
    sequences,
    delay_ms
  )
end

--- List all stored session aliases
---@return string[]
function M.list()
  return session_store.list_aliases()
end

--- List saved result files
---@return string[]
function M.list_saved()
  local save_dir = get_save_dir()
  if vim.fn.isdirectory(save_dir) == 0 then
    return {}
  end
  local files = vim.fn.glob(save_dir .. "/*.json", false, true)
  local names = {}
  for _, path in ipairs(files) do
    local name = vim.fn.fnamemodify(path, ":t:r")
    table.insert(names, name)
  end
  return names
end

--- View a saved result file
---@param name string  Name of the saved result (without .json extension)
function M.view(name)
  if not name or name == "" then
    -- List available saved results
    local saved = M.list_saved()
    if #saved == 0 then
      vim.notify("No saved results found", vim.log.levels.INFO)
    else
      vim.notify("Saved results:\n  " .. table.concat(saved, "\n  "), vim.log.levels.INFO)
    end
    return
  end

  local data, err = load_results(name)
  if not data then
    vim.notify("Failed to load '" .. name .. "': " .. (err or "unknown error"), vim.log.levels.ERROR)
    return
  end

  -- Format and display results
  local user_cost_str = data.user_cost and string.format(" (cost: %.2f)", data.user_cost) or ""
  local formatted_user_seq = data.user_seq and ffi_lib.format_sequence(data.user_seq) or "(none)"
  local output_lines = {
    "=== " .. name .. " ===",
    "",
    string.format("Position: (%d, %d) -> (%d, %d)",
      data.start_row, data.start_col,
      data.end_row, data.end_col),
    "",
    "User sequence: " .. formatted_user_seq .. user_cost_str,
    "",
    "Optimal motions:",
  }

  local optimal = data.optimal_results or {}
  for i, r in ipairs(optimal) do
    local formatted_seq = ffi_lib.format_sequence(r.seq)
    table.insert(output_lines, string.format("  %d. %s (cost: %.2f)", i, formatted_seq, r.cost or 0))
  end

  if #optimal == 0 then
    table.insert(output_lines, "  (no results)")
  end

  table.insert(output_lines, "")
  table.insert(output_lines, "Buffer context: (start, end marked with < >)")
  local lines = data.lines or {}
  for i, line in ipairs(lines) do
    -- Highlight start/end lines
    local prefix = "  "
    if i - 1 == data.start_row then
      prefix = "> "
    elseif i - 1 == data.end_row then
      prefix = "< "
    end
    table.insert(output_lines, prefix .. line)
  end

  -- Show in a scratch buffer
  vim.cmd("botright new")
  local buf = vim.api.nvim_get_current_buf()
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency"
  vim.api.nvim_buf_set_name(buf, "vimficiency://" .. name)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, output_lines)
  vim.bo[buf].modifiable = false

  -- Add 'q' to close buffer (common pattern for temporary/preview buffers)
  vim.keymap.set("n", "q", "<cmd>close<cr>", {
    buffer = buf,
    nowait = true,
    desc = "Close vimficiency view",
  })
end

return M
