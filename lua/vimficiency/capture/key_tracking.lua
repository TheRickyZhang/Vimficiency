-- lua/vimficiency/key_tracking.lua
-- `vim.on_key()` exposes executed keys, not raw physical input.
-- See `dev/lua/neovim_on_key_issues.md` for the mapping-resolution details.
local M = {}
local uv = vim.uv
local ffi_lib = require("vimficiency.ffi")
local keynorm = require("vimficiency.capture.keynorm")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

---@class VimficiencyKeyEvent
---@field t integer              # hrtime timestamp
---@field win integer            # window id (useful for multi-session matching)
---@field buf integer            # buffer id
---@field mode string            # vim mode at time of keypress
---@field key_sent_raw string    # raw key sent (for debugging)
---@field key_sent string        # keytrans'd key sent
---@field key_typed_raw string   # raw typed key (before mappings)
---@field key_typed string       # keytrans'd typed key

-- Announced admin activity: Vimfy entry points bracket themselves with
-- `begin_ignore()` / `end_ignore()` so internal keys are suppressed.

local ignoring = false

function M.begin_ignore()
	local prev = ignoring
	ignoring = true
	return prev
end

function M.end_ignore(prev)
	ignoring = prev
end

---@param get_session fun(): ActiveSession|nil
---@param reset_session fun(reason: string, level: integer)
---@param should_evict fun(session: ActiveSession): string|nil  Optional: return a reason string to drop the session; nil to keep. Runs before the window-sameness check and before the key is appended, so eviction fires on every keystroke regardless of which window is focused.
---@return integer nsid
function M.attach(get_session, reset_session, should_evict)
	local nsid = nil

	local function on_key(key, typed)
		if ignoring then return end

		local session = get_session()
		if not session then return end

		-- Eviction checks inspect `session.win`, not the current window.
		if should_evict then
			local reason = should_evict(session)
			if reason then
				reset_session(reason, vim.log.levels.WARN)
				return
			end
		end

		local curr_win = vim.api.nvim_get_current_win()
		-- Keys from other windows are ignored without aborting the session.
		if curr_win ~= session.win then return end

		typed = typed or ""
		local mode_full = vim.api.nvim_get_mode().mode
		local m = mode_full:sub(1, 1)

		-- Multi-key mapping resolution: strip the already-recorded LHS
		-- bytes, and keep `session.key_count` in sync with `#key_seq`.
		-- The global on_key's recall fan-out (session_store.strip_recall_pre_resolution)
		-- already maintains the `key_count == #key_seq` invariant for
		-- recall records; mirroring it here makes the invariant hold for
		-- manual/watch sessions too, so anything that reads `key_count`
		-- during capture (metrics, summarize_all, future live counters)
		-- stays consistent.
		if #typed > 1 and typed ~= key then
			local popped = M.strip_matching_tail(session.key_seq, typed)
			if popped > 0 then
				session.key_count = math.max(0, (session.key_count or 0) - popped)
			end
			typed = ""
		end

		if typed == "" then return end

		-- Cmdline activity is meta, not motion — drop unconditionally.
		if m == "c" then return end
		if m == "n" and key == ":" then return end

		session.key_seq[#session.key_seq + 1] = {
			t = uv.hrtime(),
			mode = mode_full,
			win = curr_win,
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = keynorm.normalize(key),
			key_typed_raw = typed,
			key_typed = keynorm.normalize(typed),
		}
		session.key_count = (session.key_count or 0) + 1
	end

	nsid = vim.on_key(on_key, nsid)
	return nsid
end

function M.detach(nsid)
	if nsid then
		vim.on_key(nil, nsid)
	end
end

--- Build a key sequence string from key events, with deduplication.
---@param key_seq VimficiencyKeyEvent[]
---@return string
function M.build_sequence(key_seq)
	return ffi_lib.build_sequence(key_seq)
end

--- Remove the trailing events whose `key_typed_raw` bytes match `typed_raw`.
---@param key_seq VimficiencyKeyEvent[]
---@param typed_raw string
---@return integer popped
function M.strip_matching_tail(key_seq, typed_raw)
	if #typed_raw == 0 or #key_seq == 0 then return 0 end
	local acc = ""
	local i = #key_seq
	while i > 0 and #acc < #typed_raw do
		local ev = key_seq[i]
		acc = (ev.key_typed_raw or "") .. acc
		i = i - 1
	end
	if acc ~= typed_raw then return 0 end
	local popped = #key_seq - i
	for j = #key_seq, i + 1, -1 do
		key_seq[j] = nil
	end
	return popped
end

-- Shared global `vim.on_key()` namespace for key-count and time-based sessions.

local global_nsid = nil
---@type table<string, {on_event: fun(event: VimficiencyKeyEvent)}>
local global_subs = {}
local global_subs_count = 0

local function ensure_global_listener()
	if global_nsid then return end

	local function on_key(key, typed)
		if ignoring then return end

		typed = typed or ""
		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)

		-- Multi-key mapping resolution: tell subscribers to strip the LHS.
		if #typed > 1 and typed ~= key then
			for _, sub in pairs(global_subs) do
				if sub.on_resolution then sub.on_resolution(typed) end
			end
			return
		end

		if typed == "" then return end
		if m == "c" then return end
		if m == "n" and key == ":" then return end

		---@type VimficiencyKeyEvent
		local event = {
			t = uv.hrtime(),
			mode = mode,
			win = vim.api.nvim_get_current_win(),
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = keynorm.normalize(key),
			key_typed_raw = typed,
			key_typed = keynorm.normalize(typed),
		}

		for _, sub in pairs(global_subs) do
			sub.on_event(event)
		end
	end

	global_nsid = vim.on_key(on_key, global_nsid)
end

--- Attach a named global key listener.
--- `on_resolution` is called on multi-key mapping resolution events.
---@param on_key_event fun(event: VimficiencyKeyEvent)
---@param name string|nil             Subscriber name; defaults to "default"
---@param on_resolution (fun(typed_raw: string))|nil  Optional strip hook
---@return boolean success
function M.attach_global(on_key_event, name, on_resolution)
	name = name or "default"
	if global_subs[name] then
		return false
	end
	global_subs[name] = { on_event = on_key_event, on_resolution = on_resolution }
	global_subs_count = global_subs_count + 1
	ensure_global_listener()
	return true
end

--- Detach a named global key listener.
---@param name string|nil  Subscriber name; defaults to "default"
function M.detach_global(name)
	name = name or "default"
	if not global_subs[name] then return end
	global_subs[name] = nil
	global_subs_count = global_subs_count - 1

	if global_subs_count == 0 and global_nsid then
		vim.on_key(nil, global_nsid)
		global_nsid = nil
	end
end

--- Check if a named global listener is active.
---@param name string|nil  Subscriber name; defaults to "default"
---@return boolean
function M.is_global_attached(name)
	name = name or "default"
	return global_subs[name] ~= nil
end

--- Tear down the shared global `vim.on_key` namespace.
function M.shutdown()
	if global_nsid then
		vim.on_key(nil, global_nsid)
		global_nsid = nil
	end
	global_subs = {}
	global_subs_count = 0
end

return M
