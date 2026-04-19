--- NeoVim is a bit unusual in that there is NO way to directly access the RAW keys that a user presses
--- Instead, all keys are passed through layers like a key mapping to give the actual executed sequence
--- on_key(key, typed) exposes a callback for when an executed key is called. However, if that key was part of a longer original typed sequence, typed could be that original sequence per each of the resulting characters with no guarantee for consistency. For example, if I had map ABC -> DEF, then for any key D, E, F, onkey(D, _) -> _ could be "" or "ABC" for each, we can't know.
--- I am not entirely sure of the inner workings, but it seems that all keys anticipating a mapping match still get caught before the total match, so if we simply ignore all callbacks where key != typed, and key.size() > 1, then we will ignore duplicates exactly.
--- For more background, see: https://github.com/neovim/neovim/issues/15527


-- lua/vimficiency/key_tracking.lua
local M = {}
local uv = vim.uv
local ffi_lib = require("vimficiency.ffi")

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

--------------------------------------------------------------------------------
-- Ignore flag: announced admin activity
--
-- Any entry point that invokes Vimfy code (the :Vimfy user command, a <Plug>
-- mapping callback, or a function built by `require('vimficiency').wrap(fn)`)
-- must bracket its body with begin_ignore/end_ignore. While the flag is set,
-- both on_key handlers below return immediately, so any key events fired by
-- the admin code itself (including nested API calls and redraw-triggered
-- synthetic keys) are suppressed.
--
-- Save/restore via the returned `prev` keeps nested invocations correct.
--
-- Admin-intent is *announced*, not detected. We deliberately do NOT probe
-- maparg() or scan mapping RHS strings at on_key time — that earlier path
-- accumulated three heuristics and still missed Lua-callback RHS. The
-- blessed bindings are `require('vimficiency').map()` (primary),
-- `<Plug>Vimfy*` maps, and `require('vimficiency').wrap(fn)`; all three
-- set `ignoring` around their bodies.
--
-- Fallback: init.lua's setup() scans existing mappings once and warns
-- about any string RHS matching `:Vimfy` / `<Cmd>Vimfy`. Pre-setup
-- mappings only; post-setup or Lua-callback RHS is invisible to the
-- scan by design. See doc-src/08-keymaps.md.
--------------------------------------------------------------------------------

local ignoring = false

--------------------------------------------------------------------------------
-- Debug log (circular buffer of every on_key decision)
--
-- Per-session and global on_key callbacks each record one entry per event
-- they see, tagging *why* the event was kept or dropped. This lets
-- `:Vimfy debug` answer questions like "was my <Space>ve recorded despite
-- being a bound Lua-callback mapping?" without guessing.
--
-- Cheap by design: bounded ring (DEBUG_LOG_CAP), tiny per-entry table,
-- no string formatting on the hot path (keytrans happens at dump time).
--------------------------------------------------------------------------------

local DEBUG_LOG_CAP = 500
---@type { src: string, key: string, typed: string, ignoring: boolean,
---        mode: string, curr_win: integer, session_win: integer|nil,
---        action: string, reason: string|nil, t_ns: integer }[]
local debug_log = {}
local debug_log_head = 1   -- 1-based index of the next slot to write
local debug_log_full = false

---@param entry table
local function push_debug(entry)
	entry.t_ns = uv.hrtime()
	debug_log[debug_log_head] = entry
	debug_log_head = debug_log_head + 1
	if debug_log_head > DEBUG_LOG_CAP then
		debug_log_head = 1
		debug_log_full = true
	end
end

--- Return the debug log in chronological order (oldest first).
---@return table[]
function M.get_debug_log()
	---@type table[]
	local out = {}
	if debug_log_full then
		for i = debug_log_head, DEBUG_LOG_CAP do out[#out + 1] = debug_log[i] end
	end
	for i = 1, debug_log_head - 1 do out[#out + 1] = debug_log[i] end
	return out
end

--- Clear the debug log.
function M.clear_debug_log()
	debug_log = {}
	debug_log_head = 1
	debug_log_full = false
end

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
		local raw_typed = typed or ""
		local raw_key   = key or ""
		local mode_full = vim.api.nvim_get_mode().mode
		local curr_win  = vim.api.nvim_get_current_win()

		---@param action string
		---@param reason string|nil
		local function log(action, reason)
			push_debug({
				src = "session",
				key = raw_key,
				typed = raw_typed,
				ignoring = ignoring,
				mode = mode_full,
				curr_win = curr_win,
				session_win = nil,
				action = action,
				reason = reason,
			})
		end

		if ignoring then log("drop", "ignoring flag") return end

		local session = get_session()
		if not session then
			log("drop", "no active session")
			return
		end

		-- Eviction checks (drift, idle, window-closed) run on every
		-- keystroke regardless of which window is focused.  The callback
		-- inspects session.win, not the current window.
		if should_evict then
			local reason = should_evict(session)
			if reason then
				log("drop", "evicted: " .. reason)
				reset_session(reason, vim.log.levels.WARN)
				return
			end
		end

		-- Keys typed in a different window (floating picker, temporary
		-- split, etc.) are not motions in the session's buffer — skip
		-- recording without aborting.  Finish-time validation is the
		-- authoritative buffer guard.
		if curr_win ~= session.win then
			push_debug({
				src = "session",
				key = raw_key, typed = raw_typed,
				ignoring = ignoring, mode = mode_full,
				curr_win = curr_win, session_win = session.win,
				action = "drop", reason = "wrong window",
			})
			return
		end

		typed = raw_typed

		local m = mode_full:sub(1, 1)

		-- Multi-key mapping resolution event: `typed` carries the full LHS,
		-- `key` carries the first expansion byte (often a Lua-callback
		-- sentinel). When the user types the LHS slowly enough for
		-- `on_key` to fire for each pending byte, those pre-resolution
		-- events are already in `key_seq`; we need to strip them here
		-- before dropping the resolution event itself. Walking back over
		-- `key_typed_raw` and concatenating rebuilds the user-typed byte
		-- stream — if the tail of `key_seq` matches `typed`, the
		-- preceding events were the mapping's LHS and belong to the
		-- `ignoring` window we never got to take.
		if #typed > 1 and typed ~= key then
			local stripped = M.strip_matching_tail(session.key_seq, typed)
			if stripped > 0 then
				log("drop", "dedup + stripped " .. stripped ..
					" pre-resolution event(s)")
			else
				log("drop", "dedup (no matching tail to strip)")
			end
			typed = ""
		end

		if typed == "" then
			return
		end

		-- Cmdline activity is meta, not motion — drop unconditionally.
		if m == "c" then
			log("drop", "cmdline mode")
			return
		end
		if m == "n" and key == ":" then
			log("drop", "pre-cmdline :")
			return
		end

		session.key_seq[#session.key_seq + 1] = {
			t = uv.hrtime(),
			mode = mode_full,
			win = curr_win,
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = vim.fn.keytrans(key),
			key_typed_raw = typed,
			key_typed = vim.fn.keytrans(typed),
		}
		push_debug({
			src = "session",
			key = raw_key, typed = raw_typed,
			ignoring = ignoring, mode = mode_full,
			curr_win = curr_win, session_win = session.win,
			action = "record", reason = nil,
		})
	end

	nsid = vim.on_key(on_key, nsid)
	return nsid
end

function M.detach(nsid)
	if nsid then
		vim.on_key(nil, nsid)
	end
end

--- Build key sequence string from key events, with deduplication.
--- Removes duplicate keys caused by operator-pending mode re-evaluation.
---@param key_seq VimficiencyKeyEvent[]
---@return string
function M.build_sequence(key_seq)
	return ffi_lib.build_sequence(key_seq)
end

--- Pop the trailing events from `key_seq` whose `key_typed_raw` bytes
--- concatenate to `typed_raw`. Returns the number of entries popped
--- (0 on mismatch or empty queue). Used when a multi-key mapping
--- resolution event fires — the pre-resolution individual-byte events
--- are already in the queue and must be retroactively removed.
---
--- Pure function: does not touch module state. Shared between the
--- per-session on_key handler and `session_store.strip_recall_pre_resolution`
--- so the behaviour stays identical in both paths, and the helper is
--- directly unit-testable without going through `vim.on_key`.
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

--------------------------------------------------------------------------------
-- Global key listener (for key-count and time-based sessions)
--------------------------------------------------------------------------------
-- One real vim.on_key namespace is shared; subscribers are addressed by name.

local global_nsid = nil
---@type table<string, {on_event: fun(event: VimficiencyKeyEvent)}>
local global_subs = {}
local global_subs_count = 0

local function ensure_global_listener()
	if global_nsid then return end

	local function on_key(key, typed)
		local raw_typed = typed or ""
		local raw_key = key or ""
		local mode = vim.api.nvim_get_mode().mode
		local curr_win = vim.api.nvim_get_current_win()

		---@param action string
		---@param reason string|nil
		local function log(action, reason)
			push_debug({
				src = "global",
				key = raw_key, typed = raw_typed,
				ignoring = ignoring, mode = mode,
				curr_win = curr_win, session_win = nil,
				action = action, reason = reason,
			})
		end

		if ignoring then log("drop", "ignoring flag") return end

		typed = raw_typed
		local m = mode:sub(1, 1)

		-- Multi-key mapping resolution (same logic as session on_key):
		-- tell every subscriber to strip the pre-resolution pending
		-- events that already reached it, then drop this event. A
		-- subscriber may ignore the signal by not implementing
		-- `on_resolution`; strip is per-subscriber because each one
		-- owns its own backing store (e.g. recall_capture writes into
		-- `session_store`'s recall ring).
		if #typed > 1 and typed ~= key then
			for _, sub in pairs(global_subs) do
				if sub.on_resolution then sub.on_resolution(typed) end
			end
			log("drop", "dedup + signaled subscribers to strip " ..
				tostring(#typed) .. " bytes")
			return
		end

		if typed == "" then log("drop", "dedup") return end
		if m == "c" then log("drop", "cmdline") return end
		if m == "n" and key == ":" then log("drop", "pre-cmdline :") return end

		---@type VimficiencyKeyEvent
		local event = {
			t = uv.hrtime(),
			mode = mode,
			win = curr_win,
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = vim.fn.keytrans(key),
			key_typed_raw = typed,
			key_typed = vim.fn.keytrans(typed),
		}

		log("record", nil)
		for _, sub in pairs(global_subs) do
			sub.on_event(event)
		end
	end

	global_nsid = vim.on_key(on_key, global_nsid)
end

--- Attach a named global key listener. Multiple listeners can coexist under
--- distinct names. Passing the same name twice fails (returns false).
---
--- `on_resolution(typed_raw)` is an optional hook: called when a multi-key
--- mapping resolution event fires (nvim's "combined LHS" event). The
--- subscriber should remove the trailing events from its backing store
--- whose `key_typed_raw` concatenates to `typed_raw`. See
--- `capture/recall.lua` for the reference implementation.
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

--- Tear down the shared global `vim.on_key` namespace. Intended for the
--- plugin reload path (`:Vimfy reload`), where we must detach this module's
--- on_key callback before niling `package.loaded[...]`; otherwise the old
--- callback keeps firing against the orphaned module's `global_subs` table.
--- Per-subscriber names are also forgotten so the new module starts clean.
function M.shutdown()
	if global_nsid then
		vim.on_key(nil, global_nsid)
		global_nsid = nil
	end
	global_subs = {}
	global_subs_count = 0
end

return M
