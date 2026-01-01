-- lua/vimficiency/init.lua
local ffi_lib = require("vimficiency.ffi")
local config = require("vimficiency.config")
local session = require("vimficiency.session")
local M = {}

-- Re-export config for backwards compatibility
M.config = config

local function set_cmd(name, fn, opts)
	opts = opts or {}
	pcall(vim.api.nvim_del_user_command, name)
	vim.api.nvim_create_user_command(name, fn, opts)
end

local function import_lua_config(user_config)
	if user_config.slice_padding then
		config.slice_padding = user_config.slice_padding
	end
	if user_config.slice_expand_to_paragraph then
		config.slice_expand_to_paragraph = user_config.slice_expand_to_paragraph
	end
	if user_config.max_search_lines then
		config.max_search_lines = user_config.max_search_lines
	end
end

function M.setup(user_config)
	-- Take lua parts
	import_lua_config(user_config)
	-- Push to C++
	ffi_lib.configure(user_config)

	set_cmd("VimficiencyStart", session.start, {})
	set_cmd("VimficiencyStop", session.finish, {})
	set_cmd("VimficiencySimulate", function(o)
		session.simulate(o.args)
	end, { nargs = 1 })

	set_cmd("VimficiencyReload", function()
		-- Rebuild the shared library
		local build_dir = vim.fn.expand("~/Projects/vimficiency/build") -- adjust as needed
		vim.notify("Rebuilding Vimficiency...", vim.log.levels.INFO)

		local result = vim.fn.system({ "cmake", "--build", build_dir, "-j" })
		if vim.v.shell_error ~= 0 then
			vim.notify("Build failed: " .. result, vim.log.levels.ERROR)
			return
		end

		-- Reloading a shared library in LuaJIT is tricky, safest to restart Neovim
		vim.notify("Rebuild complete. Restart Neovim to load new library.", vim.log.levels.WARN)
	end, {})

	set_cmd("VimficiencyDebugConfig", function()
		local debug_output = ffi_lib.debug_config()
		-- Show in a scratch buffer
		vim.cmd("botright new")
		local buf = vim.api.nvim_get_current_buf()
		vim.bo[buf].buftype = "nofile"
		vim.bo[buf].bufhidden = "wipe"
		vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.split(debug_output, "\n"))
	end, {})
end

return M
