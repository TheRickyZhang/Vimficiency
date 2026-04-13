-- lua/vimficiency/init.lua
local ffi_lib = require("vimficiency.ffi")
local config = require("vimficiency.config")
local alias_mod = require("vimficiency.alias")
local session = require("vimficiency.session")
local session_store = require("vimficiency.session_store")
local key_tracking = require("vimficiency.key_tracking")
local auto_suggest = require("vimficiency.auto_suggest")
local M = {}

-- Re-export config for backwards compatibility
M.config = config

local function set_cmd(name, fn, opts)
	opts = opts or {}
	pcall(vim.api.nvim_del_user_command, name)
	vim.api.nvim_create_user_command(name, fn, opts)
end

-- `config.lua` is the schema: any key with a default there is a Lua-side knob.
-- C++-only keys aren't in `config` and are handled by `ffi_lib.configure`.
-- Returns the set of keys claimed so setup() can warn on leftovers.
---@return table<string, true>
local function import_lua_config(user_config)
	local consumed = {}
	for k, v in pairs(user_config) do
		if config[k] ~= nil then
			config[k] = v
			consumed[k] = true
		end
	end
	return consumed
end

-- Recall alias validation delegates to the central grammar module so the
-- auto_suggest window validator can't drift from `:Vimfy end 3s` parsing.

-- Validate and normalize `user_config.auto_suggest`, writing the result to
-- `config.auto_suggest`. Errors loudly on unknown keys or malformed values
-- so typos can't silently no-op.
--
-- Accepted shapes:
--   auto_suggest = false                          -- explicit disable
--   auto_suggest = nil                            -- implicit disable
--   auto_suggest = { idle = {...},                -- at least one trigger
--                    cooldown_ms = 5000 }
--
-- Reserved trigger names (`keys`, `cost`) error out: their behavior isn't
-- implemented yet and we don't want typos to silently no-op when they ship.
local function validate_auto_suggest(raw)
	if raw == nil or raw == false then
		config.auto_suggest = false
		return
	end
	if type(raw) ~= "table" then
		error("auto_suggest must be false or a table, got " .. type(raw))
	end

	local reserved = {
		keys = "auto_suggest.keys trigger is not yet implemented",
		cost = "auto_suggest.cost trigger is not yet implemented",
	}
	local top_allowed = { idle = true, keys = true, cost = true, cooldown_ms = true }
	for k in pairs(raw) do
		if reserved[k] then error(reserved[k]) end
		if not top_allowed[k] then
			error("auto_suggest: unknown key '" .. tostring(k) ..
				"' (allowed: idle, cooldown_ms)")
		end
	end

	local normalized = { cooldown_ms = 5000 }

	if raw.idle ~= nil then
		if type(raw.idle) ~= "table" then
			error("auto_suggest.idle must be a table")
		end
		local idle_allowed = { ms = true, window = true }
		for k in pairs(raw.idle) do
			if not idle_allowed[k] then
				error("auto_suggest.idle: unknown key '" .. tostring(k) ..
					"' (allowed: ms, window)")
			end
		end
		if type(raw.idle.ms) ~= "number" or raw.idle.ms <= 0 then
			error("auto_suggest.idle.ms must be a positive number")
		end
		if not alias_mod.is_recall(raw.idle.window) then
			error("auto_suggest.idle.window must be a recall alias " ..
				"(digits, e.g. '50', or digits+s, e.g. '3s')")
		end
		normalized.idle = { ms = raw.idle.ms, window = raw.idle.window }
	end

	if raw.cooldown_ms ~= nil then
		if type(raw.cooldown_ms) ~= "number" or raw.cooldown_ms < 0 then
			error("auto_suggest.cooldown_ms must be a non-negative number")
		end
		normalized.cooldown_ms = raw.cooldown_ms
	end

	-- No triggers present → effectively disabled. Treat as false so
	-- downstream code has one "is it on" check.
	if normalized.idle == nil then
		config.auto_suggest = false
		return
	end

	config.auto_suggest = normalized
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
	usage = "end <alias>",
	fn = function(args)
		local alias = args[1]
		if not alias or alias == "" then
			vim.notify("Usage: Vimfy end <alias>", vim.log.levels.ERROR)
			return
		end
		session.finish(alias)
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

subcommands.save = {
	desc = "Save a finished session result to disk",
	usage = "save <selector>|@ [as] <name>",
	fn = function(args)
		local selector = args[1]
		-- Tolerate an optional "as" keyword between selector and name.
		-- `save foo bar` and `save foo as bar` both mean the same thing —
		-- the keyword exists for readability, not for parsing correctness.
		local name
		if args[2] == "as" then
			name = args[3]
		else
			name = args[2]
		end
		if not selector or selector == "" or not name or name == "" then
			vim.notify("Usage: Vimfy save <selector>|@ [as] <name>", vim.log.levels.ERROR)
			return
		end
		session.save(selector, name)
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

subcommands.suggest = {
	desc = "Control auto-suggest (idle trigger)",
	usage = "suggest <on|off|toggle>",
	fn = function(args)
		local action = args[1]
		local function enable_or_warn()
			if not auto_suggest.is_configured() then
				vim.notify(
					"auto_suggest has no triggers configured. Add `auto_suggest = { idle = { ms, window } }` to setup{}.",
					vim.log.levels.ERROR
				)
				return
			end
			-- Auto-suggest needs recall recording to have anything to analyze.
			-- Silently turn it on; matches the setup-time coupling.
			if not session.is_recall_enabled() then
				session.enable_recall({ quiet = true })
			end
			if auto_suggest.enable() then
				vim.notify("vimficiency auto-suggest enabled", vim.log.levels.INFO)
			else
				vim.notify("vimficiency auto-suggest already enabled", vim.log.levels.WARN)
			end
		end

		if action == "on" then
			enable_or_warn()
		elseif action == "off" then
			auto_suggest.disable()
			vim.notify("vimficiency auto-suggest disabled", vim.log.levels.INFO)
		elseif action == "toggle" then
			if auto_suggest.is_enabled() then
				auto_suggest.disable()
				vim.notify("vimficiency auto-suggest disabled", vim.log.levels.INFO)
			else
				enable_or_warn()
			end
		else
			vim.notify("Usage: Vimfy suggest <on|off|toggle>", vim.log.levels.ERROR)
		end
	end,
}

subcommands.recall = {
	desc = "Control the rolling recall ring",
	usage = "recall <on|off|toggle>",
	fn = function(args)
		local action = args[1]
		if action == "on" then
			session.enable_recall()
		elseif action == "off" then
			session.disable_recall()
		elseif action == "toggle" then
			if session.is_recall_enabled() then
				session.disable_recall()
			else
				session.enable_recall()
			end
		else
			vim.notify("Usage: Vimfy recall <on|off|toggle>", vim.log.levels.ERROR)
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
	local prev = key_tracking.begin_ignore()
	local ok, err = pcall(function()
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
	end)
	key_tracking.end_ignore(prev)
	if not ok then
		vim.notify(tostring(err), vim.log.levels.ERROR)
	end
end

--- Wrap a function so that any key events it fires are suppressed from Vimfy's
--- session tracking. Use this when binding a Lua callback that invokes Vimfy:
---
---   vim.keymap.set('n', '<leader>vs', require('vimficiency').wrap(function()
---     vim.cmd('Vimfy start a')
---   end))
---
--- For simple bindings, the exported `<Plug>VimfyX` maps already announce
--- themselves; reach for `wrap` only when the bound action needs custom Lua.
---@param fn fun(...): any
---@return fun(...): any
function M.wrap(fn)
	return function(...)
		local prev = key_tracking.begin_ignore()
		local ok, err = pcall(fn, ...)
		key_tracking.end_ignore(prev)
		if not ok then
			error(err)
		end
	end
end

--- Build a pre-wrapped callback for a subcommand invocation. Shared by
--- `<Plug>` registration and the public `vimfy.map()` helper: both need
--- "resolve this subcommand by name, forward these args, announce admin
--- activity while it runs" wrapped in one function.
---@param subcmd string         Key in `subcommands`
---@param subcmd_args string[]  Args forwarded to the subcommand's fn
---@param source string         Human-readable source for error messages
---@return fun()
local function build_subcmd_callback(subcmd, subcmd_args, source)
	return M.wrap(function()
		local cmd = subcommands[subcmd]
		if not cmd then
			vim.notify(
				"Vimficiency: unknown subcommand '" .. tostring(subcmd) .. "' from " .. source,
				vim.log.levels.ERROR
			)
			return
		end
		cmd.fn(subcmd_args)
	end)
end

--- Register a <Plug> map that invokes a subcommand with fixed arguments.
---@param name string           Suffix appended to "<Plug>Vimfy"
---@param subcmd string         Key in `subcommands`
---@param subcmd_args string[]  Args forwarded to the subcommand's fn
local function register_plug(name, subcmd, subcmd_args)
	vim.keymap.set("n", "<Plug>Vimfy" .. name,
		build_subcmd_callback(subcmd, subcmd_args, "<Plug>Vimfy" .. name),
		{ silent = true, desc = "Vimficiency " .. subcmd .. " " .. table.concat(subcmd_args, " ") })
end

--- Bind a key to a Vimfy action, with the LHS keystroke announced as admin
--- activity (so it isn't counted toward motion cost). The blessed way to
--- attach Vimfy to your own keys.
---
--- `spec` can be:
---   * A string like `"start a"` or `"save @ as quick"`. Parsed the same
---     way as a `:Vimfy` argument list; first word is the subcommand.
---   * A function (anything you'd pass to `vim.keymap.set`). Wrapped with
---     `M.wrap` so it announces admin intent around its body.
---
--- Examples:
---   vimfy.map('n', '<leader>vs', 'start a')
---   vimfy.map('n', '<leader>vq', 'save @ as quick')
---   vimfy.map('n', 'Z', function() vim.cmd('Vimfy start a') end)
---
--- Prefer this over `nnoremap X :Vimfy ...<CR>`: the latter counts the
--- LHS keystroke as motion (see docs/user/07-keymaps.md).
---@param mode string|string[]       Passthrough to vim.keymap.set
---@param lhs string                 Key sequence
---@param spec string|fun(): any     Subcommand string or Lua callback
---@param opts table|nil             Passthrough to vim.keymap.set (desc/silent/buffer/...)
function M.map(mode, lhs, spec, opts)
	opts = opts or {}
	if opts.silent == nil then opts.silent = true end

	local callback
	if type(spec) == "string" then
		local parts = vim.split(spec, "%s+", { trimempty = true })
		if #parts == 0 then
			error("vimficiency.map: empty spec string")
		end
		local subcmd = parts[1]
		local subcmd_args = {}
		for i = 2, #parts do subcmd_args[#subcmd_args + 1] = parts[i] end
		callback = build_subcmd_callback(subcmd, subcmd_args, "vimfy.map(" .. lhs .. ")")
		if not opts.desc then opts.desc = "Vimficiency " .. spec end
	elseif type(spec) == "function" then
		callback = M.wrap(spec)
		if not opts.desc then opts.desc = "Vimficiency <fn>" end
	else
		error("vimficiency.map: spec must be a string or function, got " .. type(spec))
	end

	vim.keymap.set(mode, lhs, callback, opts)
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
	if subcmd == "recall" or subcmd == "suggest" then
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, {"on", "off", "toggle"})
	end

	if subcmd == "view" then
		local saved = session.list_saved()
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, saved)
	end

	if subcmd == "start" then
		-- Manual-only — recall sessions can't be started explicitly.
		-- Offer known manual handles so the user can re-open / overwrite.
		local aliases = vim.tbl_filter(alias_mod.is_valid_manual, session.list())
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
	end

	if subcmd == "end" or subcmd == "close" or subcmd == "sim" then
		local aliases = session.list()
		for _, t in ipairs(alias_mod.TIME_HINTS) do
			table.insert(aliases, t)
		end
		return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
	end

	if subcmd == "save" then
		-- Position 2 is the selector; position 3 is either "as" or the name.
		if #args == 2 then
			local selectors = session.list()
			table.insert(selectors, "@")
			for _, t in ipairs(alias_mod.TIME_HINTS) do
				table.insert(selectors, t)
			end
			return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, selectors)
		end
		if #args == 3 then
			-- Suggest the "as" keyword for readability; user can ignore it.
			return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, { "as" })
		end
		return {}
	end

	return {}
end

-- Detect user mappings whose RHS invokes :Vimfy as a raw Ex command.
-- Those cause the LHS keystroke to count as motion (on_key fires before
-- the mapping resolves). We can't fix them automatically — `vim.on_key`
-- doesn't tell us the mapping is about to fire — but we can warn at
-- setup so the user knows to migrate to `<Plug>` or `vimfy.map()`.
--
-- Scope is best-effort by design:
--   * Only mappings defined *before* setup are visible. Post-setup
--     mappings (very common) won't be caught.
--   * Lua-callback RHS is opaque; we can only inspect string RHS.
--   * Buffer-local and filetype mappings defined later also slip by.
--
-- That's fine. This is a safety net, not a guarantee. The primary
-- contract is vimfy.map() / <Plug> + docs.
local MODES_TO_SCAN = { "n", "v", "x", "s", "o", "i", "t" }

local function scan_rhs_for_vimfy(rhs)
	if type(rhs) ~= "string" or rhs == "" then return false end
	-- Match `:Vim` (with optional leading space) and `<Cmd>Vim` forms.
	-- Case-insensitive because Vim command-name matching is.
	local lower = rhs:lower()
	if lower:match("^%s*:vim[fy]") then return true end
	if lower:match("<cmd>%s*vim[fy]") then return true end
	return false
end

local function warn_about_bad_mappings()
	local bad = {}
	for _, mode in ipairs(MODES_TO_SCAN) do
		local ok, maps = pcall(vim.api.nvim_get_keymap, mode)
		if ok and type(maps) == "table" then
			for _, m in ipairs(maps) do
				if scan_rhs_for_vimfy(m.rhs) then
					table.insert(bad, string.format("  %s %-20s %s", mode, m.lhs or "?", m.rhs))
				end
			end
		end
	end
	if #bad == 0 then return end
	table.sort(bad)
	vim.notify(
		"vimficiency: detected " .. #bad ..
		" mapping(s) whose RHS invokes :Vimfy as an Ex command.\n" ..
		"These will count the LHS keystroke as motion. Migrate to\n" ..
		"`require('vimficiency').map()` or a `<Plug>Vimfy*` map:\n" ..
		table.concat(bad, "\n"),
		vim.log.levels.WARN
	)
end

--------------------------------------------------------------------------------
-- Setup
--------------------------------------------------------------------------------

function M.setup(user_config)
	user_config = user_config or {}

	-- Auto-detect shiftwidth from Neovim (user config overrides)
	if not user_config.shiftwidth then
		user_config.shiftwidth = vim.o.shiftwidth
	end

	local lua_consumed = import_lua_config(user_config)

	-- auto_suggest goes through its own validator: the nested shape needs
	-- strict key-checking and normalization. Runs after the scalar importer
	-- so it can overwrite the raw table the importer just copied in.
	validate_auto_suggest(user_config.auto_suggest)

	local cpp_consumed = ffi_lib.configure(user_config)

	-- Fail-loud on keys that neither side claimed (typos, stale keys, etc.)
	local unknown = {}
	for k in pairs(user_config) do
		if not lua_consumed[k] and not cpp_consumed[k] then
			table.insert(unknown, tostring(k))
		end
	end
	if #unknown > 0 then
		table.sort(unknown)
		vim.notify(
			"vimficiency: unknown config keys ignored: " .. table.concat(unknown, ", "),
			vim.log.levels.WARN
		)
	end

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

	-- <Plug> entry points. Binding a user key to any of these announces the
	-- keypress as admin activity: the key itself is not counted as motion.
	for _, alias in ipairs({ "a", "b", "c", "d", "e" }) do
		local upper = alias:upper()
		register_plug("Start" .. upper, "start", { alias })
		register_plug("End"   .. upper, "end",   { alias })
		register_plug("Close" .. upper, "close", { alias })
		register_plug("Sim"   .. upper, "sim",   { alias })
	end
	register_plug("RecallOn",      "recall",  { "on" })
	register_plug("RecallOff",     "recall",  { "off" })
	register_plug("RecallToggle",  "recall",  { "toggle" })
	register_plug("SuggestOn",     "suggest", { "on" })
	register_plug("SuggestOff",    "suggest", { "off" })
	register_plug("SuggestToggle", "suggest", { "toggle" })
	register_plug("List",          "list",    {})
	register_plug("Config",        "config",  {})
	register_plug("Help",          "help",    {})

	-- auto_suggest implies the recall ring — turn both on so the user
	-- doesn't have to opt in twice. Explicit `:Vimfy recall off` /
	-- `:Vimfy suggest off` at runtime still wins. Quiet because setup()
	-- shouldn't spam notifications.
	if config.auto_suggest then
		session.enable_recall({ quiet = true })
		auto_suggest.enable()
	end

	-- Safety net: warn about any pre-existing user mappings that invoke
	-- :Vimfy via raw Ex command — see `scan_rhs_for_vimfy` for scope.
	warn_about_bad_mappings()
end

return M
