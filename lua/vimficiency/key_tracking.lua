--- NeoVim is a bit unusual in that there is NO way to directly access the RAW keys that a user presses
--- Instead, all keys are passed through layers like a key mapping to give the actual executed sequence
--- on_key(key, typed) exposes a callback for when an executed key is called. However, if that key was part of a longer original typed sequence, typed could be that original sequence per each of the resulting characters with no guarantee for consistency. For example, if I had map ABC -> DEF, then for any key D, E, F, onkey(D, _) -> _ could be "" or "ABC" for each, we can't know.
--- I am not entirely sure of the inner workings, but it seems that all keys anticipating a mapping match still get caught before the total match, so if we simply ignore all callbacks where key != typed, and key.size() > 1, then we will ignore duplicates exactly.
--- For more background, see: https://github.com/neovim/neovim/issues/15527


-- lua/vimficiency/key_tracking.lua
local M = {}
local uv = vim.uv

--- Patterns to filter out - if a mapping's RHS contains any of these, skip it
local FILTER_PATTERNS = {
	"VimficiencyStart",
	"VimficiencyStop",
	"VimficiencyFinish",
	-- Add more as needed
}

--- Check if a string contains any of the filter patterns
---@param rhs string
---@return boolean
local function should_filter(rhs)
	for _, pattern in ipairs(FILTER_PATTERNS) do
		if rhs:find(pattern, 1, true) then
			return true
		end
	end
	return false
end

--- Remove the last N entries from key_seq whose key_typed concatenates to form lhs
---@param key_seq VimficiencyKeyEvent[]
---@param lhs string The mapping LHS to remove (raw bytes)
local function remove_lhs_keys(key_seq, lhs)
	if lhs == "" or #key_seq == 0 then
		return
	end

	-- Work backwards, accumulating key_typed_raw until we match lhs
	local accumulated = ""
	local remove_from = nil

	for i = #key_seq, 1, -1 do
		local entry = key_seq[i]
		local kt = entry.key_typed_raw or ""
		if kt ~= "" then
			accumulated = kt .. accumulated
			if accumulated == lhs then
				remove_from = i
				break
			elseif #accumulated >= #lhs then
				-- Accumulated too much without matching, stop
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

---@param get_session fun(): VimficiencySession|nil
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

		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)
		if m == "c" then
			return
		end
		if m == "n" and key == ":" then
			return
		end

		typed = typed or ""

		-- DEDUPLICATION + FILTERING:
		-- When a multi-key mapping like <Space>te fires, Neovim:
		-- 1. First records individual keys (<Space>, t, e) as they're buffered
		-- 2. Then fires the mapping with typed=<Space>te (the full LHS)
		--
		-- At step 2, we can check what the mapping expands to.
		-- If it's something we want to filter (like VimficiencyStop),
		-- we remove the individual keys that were already recorded.

		if #typed > 1 and typed ~= key then
			-- This is a multi-key mapping firing
			-- Check what it maps to
			local map_mode = mode:sub(1, 1)
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

return M
