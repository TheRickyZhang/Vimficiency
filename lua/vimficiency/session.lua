local v = vim.api
local util = require("vimficiency.util")
local types = require("vimficiency.types")
local simulate = require("vimficiency.simulate")
local key_tracking = require("vimficiency.key_tracking")

local M = {}

---@type VimficiencyConfig
local config

---TODO: To eventually get continuous monitoring, we will need to support multiple sessions. High priority once a single session is confirmed to work.
---
---@type VimficiencySession|nil
local session = nil

local key_nsid = nil --- Key namespace id for registering handler

-------- Local functions BEGIN --------
local function file_exists(p)
	local st = vim.uv.fs_stat(p)
	return st and st.type == "file"
end

---@param dir string
---@return VimficiencyWriteDTO
local function initialize_and_write(dir)
	local buf = v.nvim_get_current_buf()
	local win = v.nvim_get_current_win()
	local id = util.new_id(buf)

	--- Use custom .vimf extension
	local path = dir .. "/" .. id .. ".vimf"

	local state = util.capture_state(buf, win)
	util.write_vimficiency(path, state)

	return types.new_write_dto(buf, win, id, path)
end

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

---@param c VimficiencyConfig
function M.setup(c)
	config = c
end

function M.start()
	---@type VimficiencyWriteDTO
	local res = initialize_and_write(config.start_dir)
	session = types.new_session(res.id, res.win, res.buf, res.path)

	key_nsid = key_tracking.attach(function()
    return session
  end, reset_session)

	vim.notify("vimficiency started " .. res.id, vim.log.levels.INFO)
end

function M.finish()
	if not session then
		total_failure("finish()", "no session found")
		return
	end
	local res = initialize_and_write(config.end_dir)

	if res.buf ~= session.start_buf then
		total_failure("finish()", "not in same buffer (not currently implemented)")
		return
	end

	local code, stdout, stderr = util.run_program(config.exe, session.start_path, res.path, session.key_seq)
	local id = session.id

	if code ~= 0 then
		total_failure("finish() error", "error " .. id .. "\n" .. (stderr ~= "" and stderr or stdout))
		return
	end

reset_session(
  "normal vimficiency finish with\n" ..
  "id: " .. id .. "\n" ..
  "stdout: " .. stdout .. "\n" ..
  vim.log.levels.INFO
)
end

function M.simulate(keys)
	if not session then
		util.show_output("no session found")
		return
	end
	if not file_exists(session.start_path) then
		util.show_output("start path not found")
		return
	end

	local data = util.read_vimficiency(session.start_path)
	if not data then
		return
	end

	simulate.simulate_visible(data.lines, data.row, data.col, keys, 500)
end

return M
