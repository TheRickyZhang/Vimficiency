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
	if user_config.SLICE_PADDING then
		config.SLICE_PADDING = user_config.SLICE_PADDING
	end
	if user_config.SLICE_EXPAND_TO_PARAGRAPH then
		config.SLICE_EXPAND_TO_PARAGRAPH = user_config.SLICE_EXPAND_TO_PARAGRAPH
	end
	if user_config.MAX_SEARCH_LINES then
		config.MAX_SEARCH_LINES = user_config.MAX_SEARCH_LINES
	end
end

function M.setup(user_config)
	-- Take lua parts
	import_lua_config(user_config)
	-- Push to C++
	ffi_lib.configure(user_config)

	set_cmd("VimficiencyStart", function(o)
		if not o.args or o.args == "" then
			vim.notify("VimficiencyStart requires an alias argument", vim.log.levels.ERROR)
			return
		end
		session.start(o.args)
	end, { nargs = 1 })
	set_cmd("VimficiencyStop", function(o)
		if not o.args or o.args == "" then
			vim.notify("VimficiencyStop requires an alias argument", vim.log.levels.ERROR)
			return
		end
		session.finish(o.args)
	end, { nargs = 1 })
	set_cmd("VimficiencySimulate", function(o)
		-- Parse: alias [count]
		local args = vim.split(o.args, "%s+")
		local alias = args[1]
		local count = args[2] and tonumber(args[2]) or nil
		local delay_ms = args[3] and tonumber(args[3]) or nil
		if not alias or alias == "" then
			vim.notify("VimficiencySimulate requires an alias", vim.log.levels.ERROR)
			return
		end
		session.simulate(alias, count, delay_ms)
	end, { nargs = "+" })

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
