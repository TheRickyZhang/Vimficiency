local v = vim.api
local util = require("vimficiency.util")
local types = require("vimficiency.types")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.key_tracking")
local ffi_lib = require("vimficiency.ffi")

local M = {}

-- ---@type VimficiencyConfig
-- local config

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

	local end_state = util.capture_state(buf, win)

	-- Build keyseq string
	local parts = {}
	for i = 1, #session.key_seq do
		parts[#parts + 1] = session.key_seq[i].key_typed
	end
	local keyseq_str = table.concat(parts, "")

	local ok, result = pcall(
		ffi_lib.analyze,
		session.start_state.lines,
		session.start_state.row,
		session.start_state.col,
		end_state.lines,
		end_state.row,
		end_state.col,
		keyseq_str
	)
	local id = session.id

	if not ok then
		total_failure("finish() error", "FFI error: " .. tostring(result))
		return
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

	local data = session.start_state
	simulate.simulate_visible(data.lines, data.row, data.col, keys, 500)
end

return M
