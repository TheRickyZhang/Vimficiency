--- NeoVim is a bit unusual in that there is NO way to directly access the RAW keys that a user presses
--- Instead, all keys are passed through layers like a key mapping to give the actual executed sequence
--- on_key(key, typed) exposes a callback for when an executed key is called. However, if that key was part of a longer original typed sequence, typed could be that original sequence per each of the resulting characters with no guarantee for consistency. For example, if I had map ABC -> DEF, then for any key D, E, F, onkey(D, _) -> _ could be "" or "ABC" for each, we can't know.
--- I am not entirely sure of the inner workings, but it seems that all keys anticipating a mapping match still get caught before the total match, so if we simply ignore all callbacks where key != typed, and key.size() > 1, then we will ignore duplicates exactly.
--- For more background, see: https://github.com/neovim/neovim/issues/15527


-- lua/vimficiency/key_tracking.lua
local M = {}
local uv = vim.uv

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

--- Patterns to filter out - if a mapping's RHS contains any of these, skip it
local FILTER_PATTERNS = {
	"Vimficiency",  -- Catches :Vimficiency subcommands (both forms)
	"Vimfy",        -- Catches :Vimfy subcommands (both forms)
}

--- Check if a mapping's RHS should be filtered (not recorded as user motion)
--- Filters:
--- 1. Mappings containing FILTER_PATTERNS (vimficiency commands)
--- 2. Mappings that enter command-line mode (start with : or <Cmd>)
---@param rhs string The mapping's RHS
---@return boolean
local function should_filter(rhs)
	-- Filter any mapping that enters command mode - these aren't motions
	-- Check for : at start (possibly after <silent>, <expr>, etc.)
	-- Common patterns: ":cmd", "<Cmd>cmd", "<C-u>:cmd"
	if rhs:match("^:") or rhs:match("^<[Cc]md>") or rhs:match("<[Cc]%-[Uu]>:") then
		return true
	end

	-- Filter explicit vimficiency patterns
	for _, pattern in ipairs(FILTER_PATTERNS) do
		if rhs:find(pattern, 1, true) then
			return true
		end
	end
	return false
end

--- Remove the last N entries from key_seq whose key_typed concatenates to form lhs
--- Note: When a multi-key mapping fires, the LAST character of the LHS may not be
--- recorded separately (it's consumed by the mapping trigger). So we match the
--- longest PREFIX of lhs that we can find, not requiring an exact match.
---@param key_seq VimficiencyKeyEvent[]
---@param lhs string The mapping LHS to remove (raw bytes)
local function remove_lhs_keys(key_seq, lhs)
	if lhs == "" or #key_seq == 0 then
		return
	end

	-- DEBUG: Uncomment to debug key filtering issues
	-- vim.notify(string.format("[remove_lhs_keys] lhs='%s' key_seq_len=%d", vim.fn.keytrans(lhs), #key_seq), vim.log.levels.DEBUG)

	-- Work backwards, accumulating key_typed_raw
	-- Don't check prefix at each step - collect all and find longest prefix match
	local accumulated = ""
	local remove_from = nil

	for i = #key_seq, 1, -1 do
		local entry = key_seq[i]
		local kt = entry.key_typed_raw or ""
		if kt ~= "" then
			accumulated = kt .. accumulated

			-- Check if current accumulated is a prefix of lhs
			-- We keep updating remove_from to track the longest matching prefix
			if lhs:sub(1, #accumulated) == accumulated then
				remove_from = i
			end

			if #accumulated >= #lhs then
				break
			end
		end
		-- Safety: don't look back more than 20 entries
		if #key_seq - i > 20 then
			break
		end
	end

	-- Remove entries from remove_from to end
	if remove_from then
		for i = #key_seq, remove_from, -1 do
			key_seq[i] = nil
		end
	end
end

-- Export for use by session_store global key sessions
M.remove_lhs_keys = remove_lhs_keys

---@param get_session fun(): ActiveSession|nil
---@param reset_session fun(reason: string, level: integer)
---@return integer nsid
function M.attach(get_session, reset_session)
	local nsid = nil

	local function on_key(key, typed)
		local session = get_session()
		if not session then
			return
		end

		local curr_win = vim.api.nvim_get_current_win()
		if curr_win ~= session.win then
			reset_session("Vimficiency: session aborted since window changed", vim.log.levels.ERROR)
			return
		end

		typed = typed or ""

		-- DEDUPLICATION + FILTERING (must happen BEFORE mode checks):
		-- When a multi-key mapping like <Space>te fires, Neovim:
		-- 1. First records individual keys (<Space>, t, e) as they're buffered
		-- 2. Then fires the mapping with typed=<Space>te (the full LHS)
		--
		-- At step 2, we can check what the mapping expands to.
		-- If it's something we want to filter (like VimficiencyStop),
		-- we remove the individual keys that were already recorded.
		--
		-- This MUST happen before mode checks because the mapping's RHS might
		-- start with ':' which would cause early return before we can filter.

		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)

		-- DEBUG: Uncomment to trace on_key calls
		-- vim.notify(string.format("[on_key] key='%s' typed='%s' mode='%s'", vim.fn.strtrans(key), vim.fn.strtrans(typed), mode), vim.log.levels.DEBUG)

		if #typed > 1 and typed ~= key then
			-- This is a multi-key mapping firing
			-- Check what it maps to (use 'n' for normal mode mappings)
			local map_mode = (m == "c") and "n" or m  -- If we're in cmdline, the mapping was from normal mode
			local maparg = vim.fn.maparg(vim.fn.keytrans(typed), map_mode)

			if maparg ~= "" and should_filter(maparg) then
				-- This mapping should be filtered - remove the LHS keys we already recorded
				remove_lhs_keys(session.key_seq, typed)
			end

			-- Either way, don't record the multi-key typed (we already have individual keys,
			-- or we just removed them)
			typed = ""
		end

		-- Skip recording if typed is empty (nothing meaningful to record)
		if typed == "" then
			return
		end

		-- Mode filtering for recording (but filtering above already happened)
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

--------------------------------------------------------------------------------
-- Global key listener (for key-count sessions)
--------------------------------------------------------------------------------

local global_nsid = nil

--- Attach a global key listener that broadcasts key events.
--- Only one global listener can be active at a time.
--- The callback receives (key_event: VimficiencyKeyEvent) after filtering.
---@param on_key_event fun(event: VimficiencyKeyEvent)
---@param on_filter_mapping fun(lhs_raw: string)|nil  Called when a filtered mapping is detected, with raw LHS bytes
---@return boolean success
function M.attach_global(on_key_event, on_filter_mapping)
	if global_nsid then
		return false  -- Already attached
	end

	local function on_key(key, typed)
		typed = typed or ""

		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)

		-- Skip multi-key mappings that should be filtered
		if #typed > 1 and typed ~= key then
			local map_mode = (m == "c") and "n" or m
			local maparg = vim.fn.maparg(vim.fn.keytrans(typed), map_mode)

			if maparg ~= "" and should_filter(maparg) then
				-- Filtered mapping detected - notify caller to remove LHS keys
				if on_filter_mapping then
					on_filter_mapping(typed)
				end
				return
			end

			-- Multi-key mapping - individual keys already recorded, skip the combined
			typed = ""
		end

		if typed == "" then
			return
		end

		-- Mode filtering
		if m == "c" then
			return
		end
		if m == "n" and key == ":" then
			return
		end

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

		on_key_event(event)
	end

	global_nsid = vim.on_key(on_key, global_nsid)
	return true
end

--- Detach the global key listener.
function M.detach_global()
	if global_nsid then
		vim.on_key(nil, global_nsid)
		global_nsid = nil
	end
end

--- Check if global listener is active.
---@return boolean
function M.is_global_attached()
	return global_nsid ~= nil
end

return M
