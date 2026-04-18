local v = vim.api
local alias_mod = require("vimficiency.session.alias")
local config = require("vimficiency.config")
local end_trigger = require("vimficiency.capture.end_trigger")
local util = require("vimficiency.util")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.capture.key_tracking")
local ffi_lib = require("vimficiency.ffi")
local session_store = require("vimficiency.session.store")
local result_view = require("vimficiency.session.result_view")

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
---@return string|nil path   Absolute path on success; nil on failure.
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
    return nil, err
  end
  return path, nil
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
--- Human-readable `'name' (buf N)` for an error message. Falls back to
--- `[No Name]` for unnamed buffers and surfaces buftype for scratch /
--- quickfix / help / terminal buffers so a recall captured in one is
--- instantly recognizable in the error.
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

local function compute_result_for_active(active)
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
  if not cfg or not cfg.idle then
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


--- Shared body: given a resolved active session, run the optimizer and
--- publish the result. Pins `active.id` up front so a ring rotation
--- between compute and finish can't attach the result to a different
--- record than the one we computed from.
---@param active ActiveSession
---@param alias string               Literal alias the caller used, stored with the record for `:Vimfy save @` default naming.
---@param reason FinishReason
local function do_finish(active, alias, reason)
  local id = active.id

  local result, err = compute_result_for_active(active)
  if not result then
    total_failure(active, "finish()", err or "unknown error")
    return
  end

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

--- Finish a manual (Mark/Watch) session. Recall windows go through
--- `M.recall` — see the docs for the `end`/`recall` split.
---@param alias string  The manual alias of the session to finish.
---@param reason FinishReason|nil  Why the finish was triggered. Defaults to "manual" (the `:Vimfy end` path). Watch's idle callback passes "watch_idle".
function M.finish(alias, reason)
  reason = reason or "manual"
  if not alias or alias == "" then
    vim.notify("finish() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local session_type = alias_mod.parse(alias)
  if session_type == "recall_key" or session_type == "recall_time" then
    vim.notify(
      "`:Vimfy end " .. alias .. "` is not a manual handle. " ..
      "Use `:Vimfy recall " .. alias .. "` for retrospective windows.",
      vim.log.levels.ERROR)
    return
  end
  if session_type ~= "manual" then
    vim.notify(
      "Invalid session alias '" .. alias .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').",
      vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    vim.notify("Session '" .. alias .. "' not found or already finished.", vim.log.levels.ERROR)
    return
  end

  do_finish(active, alias, reason)
end

--- Resolve a retrospective recall window and publish a result.
--- Accepts only `recall_key` (`N`) and `recall_time` (`Ns`) aliases —
--- manual handles are redirected to `M.finish` with a hint.
---@param alias string  A recall alias: `N` (keys ago) or `Ns` (seconds ago).
function M.recall(alias)
  if not alias or alias == "" then
    vim.notify(
      "recall() requires a window alias (e.g. '5' for keys ago, '3s' for seconds)",
      vim.log.levels.ERROR)
    return
  end

  local session_type = alias_mod.parse(alias)
  if session_type == "manual" then
    vim.notify(
      "`:Vimfy recall " .. alias .. "` expects a recall window (N or Ns). " ..
      "Use `:Vimfy end " .. alias .. "` to finish a manual session.",
      vim.log.levels.ERROR)
    return
  end
  if session_type ~= "recall_key" and session_type ~= "recall_time" then
    vim.notify(
      "Invalid recall alias '" .. alias .. "'. " ..
      "Expected N (keys ago) or Ns (seconds ago).",
      vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    if session_type == "recall_key" then
      vim.notify("No recall session found for '" .. alias .. "' keys ago.", vim.log.levels.ERROR)
    else
      vim.notify(
        "No recall session found within '" .. alias .. "'. " ..
        "Try a larger window or indexing by key count (e.g. 'recall 20').",
        vim.log.levels.ERROR)
    end
    return
  end

  do_finish(active, alias, "manual")
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

--- Resolve a save/store selector to a ResultSession.
--- `@` → last finished. Anything else → alias lookup in the ring.
---@param selector string
---@return ResultSession|nil result
---@return string|nil err
local function resolve_result_for_selector(selector)
  if selector == "@" then
    local result = session_store.get_last_finished_result()
    if not result then
      return nil, "No recently finished session. Run ':Vimfy end <alias>' first."
    end
    return result, nil
  end
  local result = session_store.get_result(selector)
  if not result then
    return nil, "No finished result for '" .. selector .. "'. Is the session still active?"
  end
  return result, nil
end

--- Write `result` to disk under `name`. On overwrite, warns (doesn't refuse).
--- Returns (path, err) — path non-nil on success, err non-nil on failure.
---@param name string
---@param result ResultSession
---@return string|nil path
---@return string|nil err
local function write_to_disk_with_overwrite_warn(name, result)
  local dest_path = get_save_dir() .. "/" .. name .. ".json"
  local existed = vim.fn.filereadable(dest_path) == 1
  local path, err = save_results(name, result)
  if not path then return nil, err end
  if existed then
    vim.notify("vimficiency: overwrote existing saved result [" .. name .. "]",
      vim.log.levels.WARN)
  end
  return path, nil
end

--- Save a finished session result to disk under `name`. Does NOT remove the
--- session from memory — the workspace copy remains available by its alias.
--- For the "move to storage" semantics, use `:Vimfy store` instead.
---
--- Selector can be any alias that resolves to a finished result, or `@`
--- for the most recently finished session. `name` must satisfy
--- `alias.is_valid_saved_name`. If `<name>.json` already exists, the file
--- is overwritten with a warning (never silently, never refused).
---@param selector string
---@param name string
function M.save(selector, name)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  local result, err = resolve_result_for_selector(selector)
  if not result then
    vim.notify(err or "unknown error", vim.log.levels.ERROR)
    return
  end

  local path, write_err = write_to_disk_with_overwrite_warn(name, result)
  if path then
    local display_path = vim.fn.fnamemodify(path, ":~")
    vim.notify("vimficiency saved [" .. name .. "] → " .. display_path, vim.log.levels.INFO)
  else
    vim.notify("vimficiency save failed: " .. (write_err or "unknown error"), vim.log.levels.ERROR)
  end
end

--- Move a finished session from memory to disk: save, then remove the
--- in-memory alias. Recall sessions (`@`, `N`, `Ns`) are rejected because
--- they don't have a stable manual alias to remove — save them with
--- `:Vimfy save` instead.
---
---@param selector string  Manual alias of the session to store.
---@param name string      Disk filename.
function M.store(selector, name)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  local active = session_store.get_active(selector)
  if active then
    vim.notify("Session '" .. selector .. "' is still active. Finish it with ':Vimfy end " ..
      selector .. "' first.", vim.log.levels.ERROR)
    return
  end
  local result, err = resolve_result_for_selector(selector)
  if not result then
    vim.notify(err or "unknown error", vim.log.levels.ERROR)
    return
  end

  -- Resolve selector to id so we can remove after saving. `@` is a
  -- shorthand, not an alias in the index — handle separately.
  local id
  if selector == "@" then
    id = session_store.get_last_finished_id()
  else
    id = session_store.get_id(selector)
  end
  if not id then
    vim.notify("store: could not locate session id for '" .. selector .. "'",
      vim.log.levels.ERROR)
    return
  end

  local path, write_err = write_to_disk_with_overwrite_warn(name, result)
  if not path then
    vim.notify("vimficiency store failed: " .. (write_err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  session_store.remove(id)
  local display_path = vim.fn.fnamemodify(path, ":~")
  vim.notify("vimficiency stored [" .. selector .. "] → [" .. name .. "] at " ..
    display_path .. " (removed from session)", vim.log.levels.INFO)
end

--- Load a saved result from disk into the current session under `alias`.
--- Disk copy is preserved (use `:Vimfy rm` to delete it separately). Refuses
--- if `alias` is already in use in the current session.
---
---@param name string   Disk filename to fetch from.
---@param alias string  Target manual alias in the session.
function M.fetch(name, alias)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'.",
      vim.log.levels.ERROR
    )
    return
  end
  if not alias_mod.is_valid_manual(alias) then
    vim.notify(
      "Invalid alias '" .. tostring(alias) ..
      "'. Manual aliases must be alphabetic only (e.g. `foo`).",
      vim.log.levels.ERROR
    )
    return
  end

  local data, err = load_results(name)
  if not data then
    vim.notify("vimficiency fetch failed: " .. (err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  local id, reg_err = session_store.register_fetched_result(alias, data)
  if not id then
    vim.notify("vimficiency fetch failed: " .. (reg_err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  vim.notify("vimficiency fetched [" .. name .. "] → [" .. alias .. "]",
    vim.log.levels.INFO)
end

--- Delete a saved result from disk. Only touches
--- `stdpath('data')/vimficiency/saved/<name>.json`; rejects names that
--- could escape the directory (same grammar as `:Vimfy save`).
---@param name string  Name of the saved result (without .json extension)
function M.rm(name)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  local path = get_save_dir() .. "/" .. name .. ".json"
  if vim.fn.filereadable(path) == 0 then
    vim.notify("No saved result '" .. name .. "' at " .. path, vim.log.levels.ERROR)
    return
  end

  if vim.fn.delete(path) ~= 0 then
    vim.notify("vimficiency rm failed for " .. path, vim.log.levels.ERROR)
    return
  end

  vim.notify("vimficiency removed [" .. name .. "] ← " .. path, vim.log.levels.INFO)
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

---@param alias string  Session alias or saved filename to simulate.
---@param count integer|nil  How many optimal results to show (default: all saved)
function M.simulate(alias, count)
  if not alias or alias == "" then
    vim.notify("simulate() requires a session alias or saved name", vim.log.levels.ERROR)
    return
  end

  local in_memory = session_store.get_result(alias)
  local on_disk = nil
  if alias_mod.is_valid_saved_name(alias) then
    on_disk = load_results(alias)
  end

  local result
  if in_memory and on_disk then
    -- Both exist. User's model: in-memory wins, surface a warning so they
    -- know the disk copy is also there and not being used.
    vim.notify(
      "'" .. alias .. "' exists in both session memory and on disk — " ..
      "replaying the in-memory copy. (The disk copy is untouched; fetch it " ..
      "under a different alias or `:Vimfy close " .. alias .. "` first to force a refetch.)",
      vim.log.levels.WARN)
    result = in_memory
  elseif in_memory then
    result = in_memory
  elseif on_disk then
    -- Disk-only. Implicitly fetch into the workspace so the user's mental
    -- model of "what's in my session" stays intact. Only works if `alias`
    -- is a valid manual alias — otherwise surface the ambiguity.
    if not alias_mod.is_valid_manual(alias) then
      vim.notify(
        "'" .. alias .. "' is on disk but isn't a valid manual alias " ..
        "(needs to be alphabetic). Fetch it explicitly with " ..
        "':Vimfy fetch " .. alias .. " <alpha-alias>'.",
        vim.log.levels.ERROR)
      return
    end
    local id, reg_err = session_store.register_fetched_result(alias, on_disk)
    if not id then
      vim.notify("simulate: implicit fetch failed: " .. (reg_err or "unknown error"),
        vim.log.levels.ERROR)
      return
    end
    vim.notify("vimficiency: fetched [" .. alias .. "] into session",
      vim.log.levels.INFO)
    result = on_disk
  else
    vim.notify("No results for '" .. alias .. "' in session or on disk.",
      vim.log.levels.ERROR)
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
    sequences
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
