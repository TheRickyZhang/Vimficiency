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
---@param should_evict fun(session: ActiveSession): string|nil  Optional: return a reason string to drop the session; nil to keep. Runs after the window-change check and before the key is appended, so eviction fires on the first key past a trigger threshold.
---@return integer nsid
function M.attach(get_session, reset_session, should_evict)
	local nsid = nil

	local function on_key(key, typed)
		if ignoring then return end

		local session = get_session()
		if not session then
			return
		end

		local curr_win = vim.api.nvim_get_current_win()
		if curr_win ~= session.win then
			reset_session("Vimficiency: session aborted since window changed", vim.log.levels.ERROR)
			return
		end

		if should_evict then
			local reason = should_evict(session)
			if reason then
				reset_session(reason, vim.log.levels.WARN)
				return
			end
		end

		typed = typed or ""

		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)

		-- Drop the duplicate events emitted while a multi-key mapping resolves.
		-- The individual LHS keys have already fired separately; the combined
		-- event carries the full LHS in `typed` and we don't want to record it.
		if #typed > 1 and typed ~= key then
			typed = ""
		end

		if typed == "" then
			return
		end

		-- Cmdline activity is meta, not motion — drop unconditionally.
		if m == "c" then
			return
		end
		if m == "n" and key == ":" then
			return
		end

		session.key_seq[#session.key_seq + 1] = {
			t = uv.hrtime(),
			mode = mode,
			win = curr_win,
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = vim.fn.keytrans(key),
			key_typed_raw = typed,
			key_typed = vim.fn.keytrans(typed),
		}
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
		if ignoring then return end

		typed = typed or ""

		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)

		if #typed > 1 and typed ~= key then
			typed = ""
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
			key_sent = vim.fn.keytrans(key),
			key_typed_raw = typed,
			key_typed = vim.fn.keytrans(typed),
		}

		for _, sub in pairs(global_subs) do
			sub.on_event(event)
		end
	end

	global_nsid = vim.on_key(on_key, global_nsid)
end

--- Attach a named global key listener. Multiple listeners can coexist under
--- distinct names. Passing the same name twice fails (returns false).
---@param on_key_event fun(event: VimficiencyKeyEvent)
---@param name string|nil  Subscriber name; defaults to "default"
---@return boolean success
function M.attach_global(on_key_event, name)
	name = name or "default"
	if global_subs[name] then
		return false
	end
	global_subs[name] = { on_event = on_key_event }
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

return M
