local v = vim.api
local init = require("vimficiency.init")
local util = require("vimficiency.util")
local types = require("vimficiency.types")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.key_tracking")
local ffi_lib = require("vimficiency.ffi")

local M = {}

---TODO: To eventually get continuous monitoring, we will need to support multiple sessions. High priority once a single session is confirmed to work.
---
---@type VimficiencySession|nil
local session = nil

local key_nsid = nil --- Key namespace id for registering handler

-------- Local functions BEGIN --------
local function reset_session(reason, level)
	if key_nsid then
		key_tracking.detach(key_nsid)
		key_nsid = nil
	end
	session = nil

	if reason then
		vim.schedule(function()
			vim.notify(reason, level or vim.log.levels.INFO)
		end)
	end
end

local function total_failure(title, text, notify_message, level)
	util.show_output(title, text)
	reset_session(notify_message or title, level or vim.log.levels.ERROR)
end

-------- Local functions END --------

function M.start()
	local buf = v.nvim_get_current_buf()
	local win = v.nvim_get_current_win()
	local id = util.new_id(buf)
	local start_state = util.capture_state(buf, win)
	session = types.new_session(id, win, buf, start_state)

	key_nsid = key_tracking.attach(function()
		return session
	end, reset_session)

	vim.notify("vimficiency started " .. id, vim.log.levels.INFO)
end


function M.finish()
	if not session then
		total_failure("finish()", "no session found")
		return
	end

	local buf = v.nvim_get_current_buf()
	local win = v.nvim_get_current_win()

	if buf ~= session.start_buf then
		total_failure("finish()", "not in same buffer (not currently implemented)")
		return
	end

  local start_state = session.start_state
	local end_state = util.capture_state(buf, win)

  util.check_state_inconsistencies(start_state, end_state)

  local start_search, end_search, buffer_line_count = util.get_search_boundaries(start_state.row, end_state.row)

  if end_search - start_search > init.config.max_search_lines then
    total_failure("finish()", "search range is larger than max_search_lines")
    return
  end

  local lines = vim.api.nvim_buf_get_lines(buf, start_search, end_search+1, true)

	-- Build keyseq string
	local parts = {}
	for i = 1, #session.key_seq do
		parts[#parts + 1] = session.key_seq[i].key_typed
	end
	local keyseq_str = table.concat(parts, "")

  ---@type boolean, string, string
	local ok, result, dbg = pcall(
		ffi_lib.analyze,
		lines,
    start_search == 0,
    end_search == buffer_line_count - 1,
		session.start_state.row - start_search,
		session.start_state.col,
		end_state.row - start_search,
		end_state.col,
		keyseq_str,
    start_state.top_row,
    start_state.bottom_row,
    start_state.window_height,
    start_state.scroll_amount
	)
	local id = session.id

	if not ok then
		total_failure("finish() error", "FFI error: " .. tostring(result))
		return
	end

  -- Persist debug by writing to file
  -- Note that we pass entire debug string up here since analyze logic should not know about id. There is some negligible runtime cost.
  if dbg and dbg ~= "" then
    local debug_dir = vim.fn.stdpath("data") .. "/vimficiency/debug"
    vim.fn.mkdir(debug_dir, "p")
    local debug_path = debug_dir .. "/" .. session.id .. ".txt"
    vim.fn.writefile(vim.split(dbg, "\n"), debug_path)
  end

  reset_session(
      "vimficiency finished\n" ..
      "id: " .. id .. "\n" ..
      "result: " .. result,
      vim.log.levels.INFO
  )
end


function M.simulate(keys)
	if not session then
		util.show_output("no session found")
		return
	end

  --TODO: this is not updated for shifted line logic, fix later
  --The buffer replayed on should only contain lines we searched. Not sure where lines should be persisted, since it is only known after calling end()
	local data = session.start_state
	simulate.simulate_visible({}, data.row, data.col, keys, 500)
end

return M
