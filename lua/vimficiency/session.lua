local v = vim.api
local config = require("vimficiency.config")
local util = require("vimficiency.util")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.key_tracking")
local ffi_lib = require("vimficiency.ffi")
local session_store = require("vimficiency.session_store")

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

---@param alias string
---@param title string
---@param text string
---@param notify_message string|nil
---@param level integer|nil
local function total_failure(alias, title, text, notify_message, level)
  util.show_output(title, text)
  session_store.remove(alias)
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

--- Format results for display
---@param results VimficiencyResult[]
---@param user_seq string|nil
---@return string
local function format_results_display(results, user_seq)
  local lines = {}
  if user_seq and user_seq ~= "" then
    table.insert(lines, string.format("  user: %s", user_seq))
  end
  for i, r in ipairs(results) do
    table.insert(lines, string.format("  %d. %s (%.2f)", i, r.seq, r.cost))
  end
  return table.concat(lines, "\n")
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

-------- Local functions END --------

---@param alias string  The alias for the session (required, must be manual type)
function M.start(alias)
  if not alias or alias == "" then
    vim.notify("start() requires a session alias", vim.log.levels.ERROR)
    return
  end

  -- Only manual sessions can be started explicitly
  local session_type = session_store.get_alias_type(alias)
  if session_type ~= "manual" then
    vim.notify(
      "Cannot manually start " .. session_type .. " sessions. " ..
      "Use a letter alias (a-e) for manual sessions.",
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

  local key_nsid = key_tracking.attach(function()
    return session_store.get_active(alias)
  end, function(reason, level)
    session_store.remove(alias)
    if reason then
      vim.schedule(function()
        vim.notify(reason, level or vim.log.levels.INFO)
      end)
    end
  end)

  local active = session_store.new_active_session(id, key_nsid, win, buf, start_state)
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimficiency started [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimficiency started [" .. alias .. "]", vim.log.levels.INFO)
  end
end


---@param alias string  The alias of the session to finish
---@param save_name string|nil  Optional name to save results to disk
function M.finish(alias, save_name)
  if not alias or alias == "" then
    vim.notify("finish() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    vim.notify("Session '" .. alias .. "' not found or already finished", vim.log.levels.ERROR)
    return
  end

  local buf = v.nvim_get_current_buf()
  local win = v.nvim_get_current_win()

  if buf ~= active.buf then
    total_failure(alias, "finish()", "not in same buffer (not currently implemented)")
    return
  end

  local start_state = active.start_state
  local end_state = util.capture_state(buf, win)

  util.check_state_inconsistencies(start_state, end_state)

  local start_search, end_search, buffer_line_count = util.get_search_boundaries(start_state.row, end_state.row)

  if end_search - start_search > config.MAX_SEARCH_LINES then
    total_failure(alias, "finish()", "search range is larger than MAX_SEARCH_LINES")
    return
  end

  local lines = vim.api.nvim_buf_get_lines(buf, start_search, end_search+1, true)

  -- Build keyseq string from accumulated key events
  local keyseq_raw = vim.iter(active.key_seq)
    :map(function(x) return x.key_typed end)
    :join("")

  -- Apply approximate motion conversions (gj->j, gk->k, etc.)
  local keyseq_str = apply_motion_conversions(keyseq_raw)

  -- Calculate positions (relative to search slice, 0-indexed)
  local rel_start_row = active.start_state.row - start_search
  local rel_start_col = active.start_state.col
  local rel_end_row = end_state.row - start_search
  local rel_end_col = end_state.col

  ---@type boolean, VimficiencyResult[], string
  local ok, results, dbg = pcall(
    ffi_lib.analyze,
    lines,
    start_search == 0,
    end_search == buffer_line_count - 1,
    rel_start_row,
    rel_start_col,
    rel_end_row,
    rel_end_col,
    keyseq_str,
    start_state.top_row,
    start_state.bottom_row,
    start_state.window_height,
    start_state.scroll_amount,
    config.RESULTS_CALCULATED
  )

  if not ok then
    total_failure(alias, "finish() error", "FFI error: " .. tostring(results))
    return
  end

  -- Persist debug by writing to file
  if dbg and dbg ~= "" then
    local debug_dir = vim.fn.stdpath("data") .. "/vimficiency/debug"
    vim.fn.mkdir(debug_dir, "p")
    local debug_path = debug_dir .. "/" .. active.id .. ".txt"
    vim.fn.writefile(vim.split(dbg, "\n"), debug_path)
  end

  -- Limit stored results to RESULTS_SAVED
  ---@type VimficiencyResult[]
  local optimal_results = {}
  for i = 1, math.min(#results, config.RESULTS_SAVED) do
    table.insert(optimal_results, results[i])
  end

  -- Create result and transition from active to result storage
  ---@type ResultSession
  local result = {
    lines = lines,
    start_row = rel_start_row,  -- 0-indexed, relative to lines
    start_col = rel_start_col,
    end_row = rel_end_row,      -- 0-indexed, relative to lines
    end_col = rel_end_col,
    user_seq = keyseq_str,
    optimal_results = optimal_results,
    timestamp = vim.uv.hrtime(),
  }

  -- This detaches key tracking and moves from active to result storage
  if not session_store.finish_session(alias, result) then
    total_failure(alias, "finish()", "failed to store result")
    return
  end

  -- Optionally save to disk
  local save_msg = ""
  if save_name and save_name ~= "" then
    local save_ok, save_err = save_results(save_name, result)
    if save_ok then
      save_msg = "\nsaved to: " .. save_name
    else
      save_msg = "\nsave failed: " .. (save_err or "unknown error")
    end
  end

  -- Format result summary with position and all results
  local pos_str = string.format("(%d,%d) -> (%d,%d)",
    rel_start_row, rel_start_col, rel_end_row, rel_end_col)
  local result_display = format_results_display(optimal_results, keyseq_str)
  vim.notify(
    "vimficiency finished [" .. alias .. "] " .. pos_str .. save_msg .. "\n" .. result_display,
    vim.log.levels.INFO
  )
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

  session_store.remove(alias)
  vim.notify("vimficiency closed [" .. alias .. "]", vim.log.levels.INFO)
end

--- Enable automatic key-count session tracking.
--- Creates a new session on each keypress, maintaining a rolling window.
---@return boolean success
function M.enable_key_sessions()
  local success = session_store.enable_key_sessions()
  if success then
    vim.notify("vimficiency key sessions enabled", vim.log.levels.INFO)
  else
    vim.notify("vimficiency key sessions already enabled", vim.log.levels.WARN)
  end
  return success
end

--- Disable automatic key-count session tracking.
--- Closes all active key sessions without finishing.
function M.disable_key_sessions()
  session_store.disable_key_sessions()
  vim.notify("vimficiency key sessions disabled", vim.log.levels.INFO)
end

--- Check if key sessions are enabled.
---@return boolean
function M.is_key_sessions_enabled()
  return session_store.is_key_sessions_enabled()
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
  local output_lines = {
    "=== " .. name .. " ===",
    "",
    string.format("Position: (%d, %d) -> (%d, %d)",
      data.start_row, data.start_col,
      data.end_row, data.end_col),
    "",
    "User sequence: " .. (data.user_seq or "(none)"),
    "",
    "Optimal motions:",
  }

  local optimal = data.optimal_results or {}
  for i, r in ipairs(optimal) do
    table.insert(output_lines, string.format("  %d. %s (cost: %.2f)", i, r.seq, r.cost or 0))
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
