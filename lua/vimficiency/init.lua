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
	if user_config.KEY_SESSION_CAPACITY then
		config.KEY_SESSION_CAPACITY = user_config.KEY_SESSION_CAPACITY
	end
end

--------------------------------------------------------------------------------
-- Subcommand definitions
--------------------------------------------------------------------------------

local subcommands = {}

subcommands.start = {
	desc = "Start a manual session",
	usage = "start <alias>",
	fn = function(args)
		local alias = args[1]
		if not alias or alias == "" then
			vim.notify("Usage: Vimfy start <alias>", vim.log.levels.ERROR)
			return
		end
		session.start(alias)
	end,
}

subcommands["end"] = {
	desc = "Finish a session and show results",
	usage = "end <alias> [save_name]",
	fn = function(args)
		local alias = args[1]
		local save_name = args[2]
		if not alias or alias == "" then
			vim.notify("Usage: Vimfy end <alias> [save_name]", vim.log.levels.ERROR)
			return
		end
		session.finish(alias, save_name)
	end,
}

subcommands.close = {
	desc = "Close a session without finishing",
	usage = "close <alias>",
	fn = function(args)
		local alias = args[1]
		if not alias or alias == "" then
			vim.notify("Usage: Vimfy close <alias>", vim.log.levels.ERROR)
			return
		end
		session.close(alias)
	end,
}

subcommands.sim = {
	desc = "Simulate motion sequences",
	usage = "sim <alias> [count] [delay_ms]",
	fn = function(args)
		local alias = args[1]
		local count = args[2] and tonumber(args[2]) or nil
		local delay_ms = args[3] and tonumber(args[3]) or nil
		if not alias or alias == "" then
			vim.notify("Usage: Vimfy sim <alias> [count] [delay_ms]", vim.log.levels.ERROR)
			return
		end
		session.simulate(alias, count, delay_ms)
	end,
}

subcommands.view = {
	desc = "View saved results",
	usage = "view [name]",
	fn = function(args)
		session.view(args[1])
	end,
}

subcommands.list = {
	desc = "List active sessions and saved files",
	usage = "list",
	fn = function()
		local aliases = session.list()
		local saved = session.list_saved()
		local lines = {}
		if #aliases > 0 then
			table.insert(lines, "Active sessions: " .. table.concat(aliases, ", "))
		else
			table.insert(lines, "Active sessions: (none)")
		end
		if #saved > 0 then
			table.insert(lines, "Saved results: " .. table.concat(saved, ", "))
		else
			table.insert(lines, "Saved results: (none)")
		end
		vim.notify(table.concat(lines, "\n"), vim.log.levels.INFO)
	end,
}

subcommands.key = {
	desc = "Control key session tracking",
	usage = "key <on|off|toggle>",
	fn = function(args)
		local action = args[1]
		if action == "on" then
			session.enable_key_sessions()
		elseif action == "off" then
			session.disable_key_sessions()
		elseif action == "toggle" then
			if session.is_key_sessions_enabled() then
				session.disable_key_sessions()
			else
				session.enable_key_sessions()
			end
		else
			vim.notify("Usage: Vimfy key <on|off|toggle>", vim.log.levels.ERROR)
		end
	end,
}

subcommands.reload = {
	desc = "Rebuild the C++ library",
	usage = "reload",
	fn = function()
		local build_dir = vim.fn.expand("~/Projects/vimficiency/build")
		vim.notify("Rebuilding Vimficiency...", vim.log.levels.INFO)
		local result = vim.fn.system({ "cmake", "--build", build_dir, "-j" })
		if vim.v.shell_error ~= 0 then
			vim.notify("Build failed: " .. result, vim.log.levels.ERROR)
			return
		end
		vim.notify("Rebuild complete. Restart Neovim to load new library.", vim.log.levels.WARN)
	end,
}

subcommands.config = {
	desc = "Show current configuration",
	usage = "config",
	fn = function()
		local debug_output = ffi_lib.debug_config()
		vim.cmd("botright new")
		local buf = vim.api.nvim_get_current_buf()
		vim.bo[buf].buftype = "nofile"
		vim.bo[buf].bufhidden = "wipe"
		vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.split(debug_output, "\n"))
	end,
}

subcommands.help = {
	desc = "Show help",
	usage = "help",
	fn = function()
		local lines = { "Vimficiency commands:", "" }
		for name, cmd in pairs(subcommands) do
			table.insert(lines, string.format("  Vimfy %-20s %s", cmd.usage, cmd.desc))
		end
		table.sort(lines)
		vim.notify(table.concat(lines, "\n"), vim.log.levels.INFO)
	end,
}

--------------------------------------------------------------------------------
-- Main command handler
--------------------------------------------------------------------------------

local function handle_vf_command(opts)
	local args = vim.split(opts.args, "%s+")
	local subcmd = args[1] or ""

	if subcmd == "" then
		subcommands.help.fn()
		return
	end

	local cmd = subcommands[subcmd]
	if not cmd then
		vim.notify("Unknown subcommand: " .. subcmd .. "\nRun :Vimfy help for usage", vim.log.levels.ERROR)
		return
	end

	-- Remove subcommand from args and call handler
	table.remove(args, 1)
	cmd.fn(args)
end

local function complete_vf(arg_lead, cmd_line, cursor_pos)
	local args = vim.split(cmd_line:sub(1, cursor_pos), "%s+")
	-- Remove "Vimfy" from args
	table.remove(args, 1)

	if #args <= 1 then
		-- Complete subcommand
		local matches = {}
		for name, _ in pairs(subcommands) do
			if name:find("^" .. arg_lead) then
				table.insert(matches, name)
			end
		end
		table.sort(matches)
		return matches
	end

	local subcmd = args[1]

	-- Subcommand-specific completions
	if subcmd == "key" then
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, {"on", "off", "toggle"})
	end

	if subcmd == "view" then
		local saved = session.list_saved()
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, saved)
	end

	if subcmd == "start" or subcmd == "end" or subcmd == "close" or subcmd == "sim" then
		local aliases = session.list()
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
	end

	return {}
end

--------------------------------------------------------------------------------
-- Setup
--------------------------------------------------------------------------------

function M.setup(user_config)
	user_config = user_config or {}

	-- Take lua parts
	import_lua_config(user_config)
	-- Push to C++
	ffi_lib.configure(user_config)

	-- Main unified commands (both prefixes work)
	set_cmd("Vimfy", handle_vf_command, {
		nargs = "*",
		complete = complete_vf,
		desc = "Vimficiency motion optimizer",
	})

	set_cmd("Vimficiency", handle_vf_command, {
		nargs = "*",
		complete = complete_vf,
		desc = "Vimficiency motion optimizer",
	})
end

return M
