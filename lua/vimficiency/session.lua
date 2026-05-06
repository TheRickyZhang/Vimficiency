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
local sequence_display = require("vimficiency.sequence_display")

local M = {}

-------- Local functions BEGIN --------

-- Approximate screen-line motions with buffer-line motions.
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

---@param record VF.Session.Record   Session being finished (must have id + start_kind).
---@param title string
---@param text string
---@param notify_message string|nil
---@param level integer|nil
local function total_failure(record, title, text, notify_message, level)
  util.show_output(title, text, {
    ui_keys = {
      title = "Vimfy Scratch Output Keys",
      docs = true,
    },
  })
  -- Manual sessions are removed on failure; recall/suggest windows are not.
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

--- Sentinel for "intentionally-empty array". Pairs with `vim.empty_dict()`
--- for objects. Lua can't distinguish `{}`-as-array from `{}`-as-object,
--- so `encode_pretty` rejects bare empty tables — the save path forces
--- callers to disambiguate, eliminating silent shape drift.
local EMPTY_ARRAY_MT = { __jsontype = "array" }
local function empty_array()
  return setmetatable({}, EMPTY_ARRAY_MT)
end
M.empty_array = empty_array

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
    events = #events > 0 and events or empty_array(),
  }
end

--- Encode `value` as JSON with 2-space indentation.
--- `vim.json.encode` has no indent option, so we walk the structure
--- ourselves and delegate leaf encoding to it. Object keys are sorted
--- for deterministic output.
---@param value any
---@param level integer|nil
---@return string
local function encode_pretty(value, level)
  if type(value) ~= "table" then
    return vim.json.encode(value)
  end
  level = level or 0
  local pad = string.rep("  ", level)
  local inner = string.rep("  ", level + 1)
  if next(value) == nil then
    local mt = getmetatable(value)
    if mt and mt.__jsontype == "array" then return "[]" end
    -- `vim.empty_dict()` is detected via `vim.islist` (which returns
    -- false for the dict marker) without depending on Neovim's internal
    -- metatable shape.
    if not vim.islist(value) then return "{}" end
    error(
      "encode_pretty: bare empty table is shape-ambiguous; " ..
      "use vim.empty_dict() for objects or session.empty_array() for arrays"
    )
  end
  if vim.islist(value) then
    local parts = {}
    for _, item in ipairs(value) do
      parts[#parts + 1] = inner .. encode_pretty(item, level + 1)
    end
    return "[\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "]"
  end
  local keys = {}
  for k in pairs(value) do keys[#keys + 1] = k end
  table.sort(keys)
  local parts = {}
  for _, k in ipairs(keys) do
    parts[#parts + 1] = inner
      .. vim.json.encode(tostring(k))
      .. ": "
      .. encode_pretty(value[k], level + 1)
  end
  return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
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
  local lines = vim.split(encode_pretty(data), "\n")
  local ok, err = pcall(vim.fn.writefile, lines, path)
  if not ok then
    return nil, err
  end
  return path, nil
end

--- Validate a decoded saved-result table.
--- Only require the fields `simulate_compare()` actually reads.
---@param data any
---@return string|nil err  nil = valid; non-nil = human-readable reason
local function validate_disk_result(data)
  if type(data) ~= "table" then
    return "top-level must be a JSON object"
  end
  if type(data.lines) ~= "table" or #data.lines == 0 then
    return "missing or empty 'lines' field"
  end
  if data.goal_lines ~= nil and type(data.goal_lines) ~= "table" then
    return "non-array 'goal_lines' field"
  end
  if type(data.start_row) ~= "number" then
    return "missing or non-numeric 'start_row' field"
  end
  if type(data.start_col) ~= "number" then
    return "missing or non-numeric 'start_col' field"
  end
  if data.has_lines_above ~= nil and type(data.has_lines_above) ~= "boolean" then
    return "non-boolean 'has_lines_above' field"
  end
  if data.has_lines_below ~= nil and type(data.has_lines_below) ~= "boolean" then
    return "non-boolean 'has_lines_below' field"
  end
  if data.window_height ~= nil and type(data.window_height) ~= "number" then
    return "non-numeric 'window_height' field"
  end
  if data.scroll_amount ~= nil and type(data.scroll_amount) ~= "number" then
    return "non-numeric 'scroll_amount' field"
  end
  if type(data.optimal_results) ~= "table" then
    return "missing or non-array 'optimal_results' field"
  end
  for i, r in ipairs(data.optimal_results) do
    if type(r) ~= "table" then
      return string.format("optimal_results[%d] is not an object", i)
    end
    if type(r.seq) ~= "string" then
      return string.format("optimal_results[%d] missing or non-string 'seq'", i)
    end
    if type(r.cost) ~= "number" then
      return string.format("optimal_results[%d] missing or non-numeric 'cost'", i)
    end
  end
  return nil
end

--- Load results from JSON file and validate the schema.
--- The third return value distinguishes missing files from broken ones.
---@param name string
---@return table|nil data
---@return string|nil error
---@return boolean   is_missing
local function load_results(name)
  local path = get_save_dir() .. "/" .. name .. ".json"
  if vim.fn.filereadable(path) == 0 then
    return nil, "File not found: " .. path, true
  end
  local lines = vim.fn.readfile(path)
  local json_str = table.concat(lines, "\n")
  local ok, data = pcall(vim.json.decode, json_str)
  if not ok then
    return nil, "Failed to parse JSON: " .. tostring(data), false
  end
  local schema_err = validate_disk_result(data)
  if schema_err then
    return nil, "Malformed result file: " .. schema_err, false
  end
  return data, nil, false
end

--- Human-readable `'name' (buf N)` for error messages.
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

--- Build a VF.Session.Result from an active session.
--- Returns nil plus an error string on failure.
---@param active VF.Session.Active
---@return VF.Session.Result|nil result
---@return string|nil err
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

  -- Recall may outlive its original window; fall back to the current one.
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

  local optimizer_overrides = ffi_lib.encode_optimizer_overrides({ shared = config.optimizer })

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
    config.RESULTS_CALCULATED,
    optimizer_overrides
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

  ---@type VF.Optimizer.Result[]
  local optimal_results = {}
  for i = 1, math.min(#results, config.RESULTS_SAVED) do
    table.insert(optimal_results, results[i])
  end
  if #optimal_results == 0 then
    optimal_results = empty_array()
  end

  ---@type VF.Session.Result
  local result = {
    lines = initial_slice,
    goal_lines = goal_slice,
    start_row = rel_start_row,
    start_col = rel_start_col,
    end_row = rel_end_row,
    end_col = rel_end_col,
    has_lines_above = has_lines_above,
    has_lines_below = has_lines_below,
    window_height = start_state.window_height,
    scroll_amount = start_state.scroll_amount,
    user_seq = keyseq_str,
    user_cost = user_cost,
    optimal_results = optimal_results,
    capture_debug = build_capture_debug(active.key_seq, keyseq_raw, keyseq_str),
    start_time = active.time_started,
    key_count = #active.key_seq,
    timestamp = vim.uv.hrtime(),
  }
  return result, nil
end

--- Public alias for the shared "active session -> result" path.
---@param active VF.Session.Active
---@return VF.Session.Result|nil, string|nil
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
---@param session VF.Session.Active  Must have start_state.row and (optionally) key_seq
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

  -- Only manual aliases can be started explicitly.
  if not alias_mod.is_valid_manual(alias) then
    vim.notify(
      "Invalid session alias '" .. alias .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').",
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

  -- Capture the stable id in the closures below.
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
        return "Vimfy: session [" .. alias .. "] dropped — window closed"
      end
      local cursor = v.nvim_win_get_cursor(session.win)
      local reason = M.manual_should_evict(
        session, cursor[1] - 1, vim.uv.hrtime()
      )
      if reason then
        return "Vimfy: session [" .. alias .. "] dropped — " .. reason
      end
      return nil
    end
  )

  local active = session_store.new_active_session(
    id, key_nsid, win, buf, start_state, "manual", "manual")
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimfy started [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimfy started [" .. alias .. "]", vim.log.levels.INFO)
  end
end


--- Watch: manual start, auto end through the idle trigger.
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
        return "Vimfy: watch [" .. alias .. "] dropped — window closed"
      end
      local cursor = v.nvim_win_get_cursor(session.win)
      local reason = M.manual_should_evict(
        session, cursor[1] - 1, vim.uv.hrtime()
      )
      if reason then
        return "Vimfy: watch [" .. alias .. "] dropped — " .. reason
      end
      return nil
    end
  )

  local active = session_store.new_active_session(
    id, key_nsid, win, buf, start_state, "manual", "auto")

  -- Scope the trigger name to this session id so watches can coexist.
  local disarm = end_trigger.arm_idle({
    name        = "watch_" .. id,
    idle_ms     = cfg.idle.ms,
    cooldown_ms = cfg.cooldown_ms,
    on_fire     = function()
      -- Re-resolve through the store in case the alias was rebound.
      local rec = session_store.get_active(alias)
      if not rec or rec.id ~= id then return false end
      M.finish(alias, "watch_idle")
      return true
    end,
  })

  if not disarm then
    key_tracking.detach(key_nsid)
    vim.notify("vimfy watch [" .. alias .. "] failed to arm idle trigger", vim.log.levels.ERROR)
    return
  end

  active.watch_disarm = disarm
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimfy watching [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimfy watching [" .. alias .. "]", vim.log.levels.INFO)
  end
end


--- Shared finish path for a resolved active session.
---@param active VF.Session.Active
---@param alias string               Literal alias the caller used, stored with the record for `:Vimfy save @` default naming.
---@param reason VF.Session.FinishReason
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

  local header = "vimfy finished [" .. alias .. "] "
    .. result_view.format_position(result)
    .. result_view.format_reason_suffix(result)
  local body = result_view.format_body(result)
  vim.notify(header .. "\n" .. table.concat(body, "\n"), vim.log.levels.INFO)
end

--- Finish a manual Mark/Watch session.
---@param alias string  The manual alias of the session to finish.
---@param reason VF.Session.FinishReason|nil  Why the finish was triggered. Defaults to "manual" (the `:Vimfy finish` path). Watch's idle callback passes "watch_idle".
function M.finish(alias, reason)
  reason = reason or "manual"
  if not alias or alias == "" then
    vim.notify("finish() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local session_type = alias_mod.parse(alias)
  if session_type == "recall_key" or session_type == "recall_time" then
    vim.notify(
      "`:Vimfy finish " .. alias .. "` is not a manual handle. " ..
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

--- Resolve a recall window and publish a result.
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
      "Use `:Vimfy finish " .. alias .. "` to finish a manual session.",
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

--- Default filename for `:Vimfy save <selector>` when the name is omitted.
---@param selector string
---@return string|nil
function M.default_save_name(selector)
  if selector == "@" then
    return session_store.get_last_finished_alias()
  end
  return selector
end

--- Resolve a save/store selector to a VF.Session.Result.
--- `@` → last finished. Anything else → alias lookup in the ring.
---@param selector string
---@return VF.Session.Result|nil result
---@return string|nil err
local function resolve_result_for_selector(selector)
  if selector == "@" then
    local result = session_store.get_last_finished_result()
    if not result then
      return nil, "No recently finished session. Run ':Vimfy finish <alias>' first."
    end
    return result, nil
  end
  local result = session_store.get_result(selector)
  if not result then
    return nil, "No finished result for '" .. selector .. "'. Is the session still active?"
  end
  return result, nil
end

--- Public resolver for any finished-result selector used by save/store/explore.
--- First checks in-memory session results. If missing and the selector is a
--- valid saved name, falls back to the on-disk JSON result.
---@param selector string
---@return VF.Session.Result|nil result
---@return string|nil err
function M.resolve_result(selector)
  local result, err = resolve_result_for_selector(selector)
  if result then return result, nil end

  if selector ~= "@" and alias_mod.is_valid_saved_name(selector) then
    local data, load_err, is_missing = load_results(selector)
    if data then
      return data, nil
    end
    if not is_missing then
      return nil, load_err or "unknown error"
    end
  end

  return nil, err
end

--- Write `result` to disk under `name`. On overwrite, warns (doesn't refuse).
--- Returns (path, err) — path non-nil on success, err non-nil on failure.
---@param name string
---@param result VF.Session.Result
---@return string|nil path
---@return string|nil err
local function write_to_disk_with_overwrite_warn(name, result)
  local dest_path = get_save_dir() .. "/" .. name .. ".json"
  local existed = vim.fn.filereadable(dest_path) == 1
  local path, err = save_results(name, result)
  if not path then return nil, err end
  if existed then
    vim.notify("vimfy: overwrote existing saved result [" .. name .. "]",
      vim.log.levels.WARN)
  end
  return path, nil
end

--- Save a finished session result to disk under `name`.
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
    vim.notify("vimfy saved [" .. name .. "] → " .. display_path, vim.log.levels.INFO)
  else
    vim.notify("vimfy save failed: " .. (write_err or "unknown error"), vim.log.levels.ERROR)
  end
end

--- Move a finished session from memory to disk.
---@param selector string  Any selector resolvable to a finished result.
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

  -- Active sessions must be finished before they can be stored.
  if session_store.get_active(selector) then
    -- Recall ring records are also `status = "active"` until `:Vimfy recall`
    -- reifies them, so route the user to the correct command for their
    -- alias kind — `:Vimfy finish` rejects recall aliases outright.
    local kind = alias_mod.parse(selector)
    local resolve_cmd = (kind == "recall_key" or kind == "recall_time")
      and (":Vimfy recall " .. selector)
      or  (":Vimfy finish " .. selector)
    vim.notify("Session '" .. selector .. "' is still active. Finish it with '" ..
      resolve_cmd .. "' first.", vim.log.levels.ERROR)
    return
  end

  -- Resolve the selector once so recall alias drift cannot race the write.
  local id
  if selector == "@" then
    id = session_store.get_last_finished_id()
  else
    id = session_store.get_id(selector)
  end
  if not id then
    vim.notify("No finished result for '" .. selector ..
      "'. Is the session still active?", vim.log.levels.ERROR)
    return
  end

  local result = session_store.get_result_by_id(id)
  if not result then
    vim.notify("No finished result for '" .. selector .. "'.", vim.log.levels.ERROR)
    return
  end

  local path, write_err = write_to_disk_with_overwrite_warn(name, result)
  if not path then
    vim.notify("vimfy store failed: " .. (write_err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  session_store.remove(id)
  local display_path = vim.fn.fnamemodify(path, ":~")
  vim.notify("vimfy stored [" .. selector .. "] → [" .. name .. "] at " ..
    display_path .. " (removed from session)", vim.log.levels.INFO)
end

--- Load a saved result from disk into the current session under `alias`.
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
    vim.notify("vimfy fetch failed: " .. (err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  local id, reg_err = session_store.register_fetched_result(alias, data)
  if not id then
    vim.notify("vimfy fetch failed: " .. (reg_err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  vim.notify("vimfy fetched [" .. name .. "] → [" .. alias .. "]",
    vim.log.levels.INFO)
end

--- Delete a saved result from disk.
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
    vim.notify("vimfy rm failed for " .. path, vim.log.levels.ERROR)
    return
  end

  vim.notify("vimfy removed [" .. name .. "] ← " .. path, vim.log.levels.INFO)
end

--- Rename the manual alias of a finished active session. Leaves the
--- underlying record untouched; only the lookup name changes.
---@param old_alias string
---@param new_alias string
---@return boolean ok
---@return string|nil err
function M.rename_active(old_alias, new_alias)
  if not alias_mod.is_valid_manual(old_alias) then
    return false, "source alias must be a manual (alphabetic) alias"
  end
  return session_store.rename_manual_alias(old_alias, new_alias)
end

--- Duplicate a finished active session's result under a new manual alias.
---@param src_alias string
---@param dst_alias string
---@return boolean ok
---@return string|nil err
function M.duplicate_active(src_alias, dst_alias)
  if not alias_mod.is_valid_manual(dst_alias) then
    return false, "target alias must be a manual (alphabetic) alias"
  end
  local result = session_store.get_result(src_alias)
  if not result then
    return false, "no finished result for '" .. tostring(src_alias) .. "'"
  end
  local _, reg_err = session_store.register_fetched_result(dst_alias, result)
  if reg_err then return false, reg_err end
  return true, nil
end

--- Rename a saved result file on disk. Refuses to overwrite an existing target.
---@param old_name string
---@param new_name string
---@return boolean ok
---@return string|nil err
function M.rename_saved(old_name, new_name)
  if not alias_mod.is_valid_saved_name(old_name) then
    return false, "Invalid source name '" .. tostring(old_name) .. "'."
  end
  if not alias_mod.is_valid_saved_name(new_name) then
    return false, "Invalid target name '" .. tostring(new_name) .. "'."
  end
  if old_name == new_name then
    return false, "Source and target names are identical."
  end
  local src = get_save_dir() .. "/" .. old_name .. ".json"
  local dst = get_save_dir() .. "/" .. new_name .. ".json"
  if vim.fn.filereadable(src) == 0 then
    return false, "No saved result '" .. old_name .. "' at " .. src
  end
  if vim.fn.filereadable(dst) == 1 then
    return false, "Target '" .. new_name .. "' already exists."
  end
  local ok, err = vim.uv.fs_rename(src, dst)
  if not ok then
    return false, "rename failed: " .. tostring(err)
  end
  return true, nil
end

--- Duplicate a saved result file on disk. Refuses to overwrite an existing target.
---@param src_name string
---@param dst_name string
---@return boolean ok
---@return string|nil err
function M.duplicate_saved(src_name, dst_name)
  if not alias_mod.is_valid_saved_name(src_name) then
    return false, "Invalid source name '" .. tostring(src_name) .. "'."
  end
  if not alias_mod.is_valid_saved_name(dst_name) then
    return false, "Invalid target name '" .. tostring(dst_name) .. "'."
  end
  if src_name == dst_name then
    return false, "Source and target names are identical."
  end
  local src = get_save_dir() .. "/" .. src_name .. ".json"
  local dst = get_save_dir() .. "/" .. dst_name .. ".json"
  if vim.fn.filereadable(src) == 0 then
    return false, "No saved result '" .. src_name .. "' at " .. src
  end
  if vim.fn.filereadable(dst) == 1 then
    return false, "Target '" .. dst_name .. "' already exists."
  end
  local ok, err = vim.uv.fs_copyfile(src, dst, nil)
  if not ok then
    return false, "copy failed: " .. tostring(err)
  end
  return true, nil
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

  -- `remove()` handles key-tracking teardown.
  session_store.remove(active.id)
  vim.notify("vimfy closed [" .. alias .. "]", vim.log.levels.INFO)
end

---@param alias string  Session alias or saved filename to simulate.
---@param count integer|nil  One-shot override for the total number of
--- side-by-side panes (1..4). When omitted, the settings-store
--- `window_count` is used (defaults to 2). The user pane — when
--- `include_user_sequence` is on — consumes one of the slots.
function M.simulate(alias, count)
  if not alias or alias == "" then
    vim.notify("simulate() requires a session alias or saved name", vim.log.levels.ERROR)
    return
  end

  local in_memory = session_store.get_result(alias)
  local on_disk = nil
  if alias_mod.is_valid_saved_name(alias) then
    local data, err, is_missing = load_results(alias)
    if data then
      on_disk = data
    elseif not is_missing then
      vim.notify("simulate: " .. (err or "unknown error"), vim.log.levels.ERROR)
      return
    end
  end

  local result
  if in_memory and on_disk then
    vim.notify(
      "'" .. alias .. "' exists in both session memory and on disk — " ..
      "replaying the in-memory copy. (The disk copy is untouched; " ..
      "`:Vimfy fetch " .. alias .. " <other-alias>` to inspect it " ..
      "separately, or `:Vimfy store " .. alias .. " <new-name>` to move " ..
      "the in-memory copy aside so future sims see the disk copy.)",
      vim.log.levels.WARN)
    result = in_memory
  elseif in_memory then
    result = in_memory
  elseif on_disk then
    -- If the alias is a valid manual handle, opportunistically register
    -- the on-disk result into the workspace so the user can refer to it
    -- again by the same alias later (e.g. `:Vimfy store`, subsequent
    -- sims). For non-alphabetic saved names (e.g. `my-name`) we skip the
    -- implicit fetch and simulate directly from the loaded data —
    -- replaying doesn't need a workspace entry, and the previous
    -- behavior here was to error out and ask the user to re-run via
    -- `:Vimfy fetch <name> <alpha-alias>`, which was user-hostile for
    -- what is fundamentally a read-only operation.
    if alias_mod.is_valid_manual(alias) then
      local id, reg_err = session_store.register_fetched_result(alias, on_disk)
      if id then
        vim.notify("vimfy: fetched [" .. alias .. "] into session",
          vim.log.levels.INFO)
      else
        vim.notify("vimfy: replaying '" .. alias .. "' directly from disk " ..
          "(implicit fetch failed: " .. (reg_err or "unknown error") ..
          "). Use `:Vimfy fetch " .. alias .. " <alias>` to keep it in the workspace.",
          vim.log.levels.WARN)
      end
    end
    result = on_disk
  else
    vim.notify("No results for '" .. alias .. "' in session or on disk.",
      vim.log.levels.ERROR)
    return
  end

  local function fmt_cost(c)
    return c and string.format("%.2f", c) or nil
  end

  -- Build the full sequence pool once. simulate owns the display policy
  -- (which pane shows what, how many panes are visible) and reads play
  -- settings internally, so session does not need a refresh hook.
  local optimal_results = result.optimal_results or {}
  local user_seq = result.user_seq or ""
  local first_optimal = optimal_results[1] and optimal_results[1].seq or ""

  ---@type { seq: string, cost: string? } | nil
  local user_item = nil
  if user_seq ~= "" and user_seq ~= first_optimal then
    user_item = { seq = user_seq, cost = fmt_cost(result.user_cost) }
  end

  ---@type { seq: string, cost: string? }[]
  local suggestions = {}
  for _, r in ipairs(optimal_results) do
    table.insert(suggestions, { seq = r.seq, cost = fmt_cost(r.cost) })
  end

  if user_item == nil and #suggestions == 0 then
    vim.notify("No sequences to simulate", vim.log.levels.WARN)
    return
  end

  simulate.simulate_compare(
    result.lines,
    result.start_row,
    result.start_col,
    {
      user = user_item,
      suggestions = suggestions,
    },
    {
      label   = alias,
      end_row = result.end_row,
      end_col = result.end_col,
      -- One-shot window_count override from `:Vimfy play <alias> <N>`.
      -- Applied only on first open; subsequent relayouts follow the
      -- settings store.
      initial_window_count = count,
    }
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
  local output_lines = {
    "=== " .. name .. " ===",
    "",
    string.format("Position: (%d, %d) -> (%d, %d)",
      data.start_row, data.start_col,
      data.end_row, data.end_col),
    "",
  }
  if data.user_seq and data.user_seq ~= "" then
    vim.list_extend(output_lines,
      sequence_display.prefixed_lines("User sequence: ", data.user_seq, nil, user_cost_str))
  else
    table.insert(output_lines, "User sequence: (none)" .. user_cost_str)
  end
  table.insert(output_lines, "")
  table.insert(output_lines, "Optimal motions:")

  local optimal = data.optimal_results or {}
  for i, r in ipairs(optimal) do
    vim.list_extend(output_lines,
      sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq, nil,
        string.format(" (cost: %.2f)", r.cost or 0)))
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

  util.set_buffer_keymaps(buf, util.with_standard_ui_keymaps({
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close vimfy view", nowait = true },
  }, {
    title = "Vimfy Saved Result Keys",
    docs = true,
  }))
end

return M
