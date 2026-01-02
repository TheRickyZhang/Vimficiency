local v = vim.api
local config = require("vimficiency.config")
local util = require("vimficiency.util")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.key_tracking")
local ffi_lib = require("vimficiency.ffi")
local session_store = require("vimficiency.session_store")

local M = {}

-------- Local functions BEGIN --------

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
function M.finish(alias)
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
  local keyseq_str = vim.iter(active.key_seq)
  :map(function(x) return x.key_typed end)
  :join("")

  ---@type boolean, string[], string
  local ok, sequences, dbg = pcall(
    ffi_lib.analyze,
    lines,
    start_search == 0,
    end_search == buffer_line_count - 1,
    active.start_state.row - start_search,
    active.start_state.col,
    end_state.row - start_search,
    end_state.col,
    keyseq_str,
    start_state.top_row,
    start_state.bottom_row,
    start_state.window_height,
    start_state.scroll_amount,
    config.RESULTS_CALCULATED
  )

  if not ok then
    total_failure(alias, "finish() error", "FFI error: " .. tostring(sequences))
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
  local optimal_seqs = {}
  for i = 1, math.min(#sequences, config.RESULTS_SAVED) do
    table.insert(optimal_seqs, sequences[i])
  end

  -- Create result and transition from active to result storage
  ---@type ResultSession
  local result = {
    lines = lines,
    start_row = active.start_state.row - start_search,  -- 0-indexed
    start_col = active.start_state.col,
    end_row = end_state.row - start_search,  -- 0-indexed
    end_col = end_state.col,
    user_seq = keyseq_str,
    optimal_seqs = optimal_seqs,
    timestamp = vim.uv.hrtime(),
  }

  -- This detaches key tracking and moves from active to result storage
  if not session_store.finish_session(alias, result) then
    total_failure(alias, "finish()", "failed to store result")
    return
  end

  local result_summary = #optimal_seqs > 0 and optimal_seqs[1] or "(no results)"
  vim.notify(
    "vimficiency finished [" .. alias .. "]\n" ..
    "best: " .. result_summary .. " (" .. #optimal_seqs .. " results saved)",
    vim.log.levels.INFO
  )
end


---@param alias string  The alias of the session to simulate
---@param count integer|nil  How many optimal results to show (default: all saved)
---@param delay_ms integer|nil  Delay between steps in ms (default 300)
function M.simulate(alias, count, delay_ms)
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
  local first_optimal = result.optimal_seqs and result.optimal_seqs[1] or ""

  if user_seq ~= "" and user_seq ~= first_optimal then
    table.insert(sequences, user_seq)
  end

  -- Add optimal sequences (limited by count if provided)
  local optimal_seqs = result.optimal_seqs or {}
  local num_to_show = count or #optimal_seqs
  for i = 1, math.min(num_to_show, #optimal_seqs) do
    table.insert(sequences, optimal_seqs[i])
  end

  if #sequences == 0 then
    vim.notify("No sequences to simulate", vim.log.levels.WARN)
    return
  end

  delay_ms = delay_ms or 300

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

return M
