--- NeoVim is a bit unusual in that there is NO way to directly access the RAW keys that a user presses
--- Instead, all keys are passed through layers like a key mapping to give the actual executed sequence
--- on_key(key, typed) exposes a callback for when an executed key is called. However, if that key was part of a longer original typed sequence, typed could be that original sequence per each of the resulting characters with no guarantee for consistency. For example, if I had map ABC -> DEF, then for any key D, E, F, onkey(D, _) -> _ could be "" or "ABC" for each, we can't know.
--- I am not entirely sure of the inner workings, but it seems that all keys anticipating a mapping match still get caught before the total match, so if we simply ignore all callbacks where key != typed, and key.size() > 1, then we will ignore duplicates exactly.
--- For more background, see: https://github.com/neovim/neovim/issues/15527


-- lua/vimficiency/key_tracking.lua
local M = {}
local uv = vim.uv

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

		-- DEDUPLICATION:
		-- When a multi-key mapping like <Space>te fires, Neovim:
		-- 1. First records individual keys (<Space>, t, e) as they're buffered
		-- 2. Then fires the mapping with typed=<Space>te (the full LHS)
		--
		-- We've already recorded the individual keys, so skip the multi-key LHS.
		-- Single-key mappings (like j -> gj) don't have this problem because
		-- there's no buffering—the mapping fires immediately.
		--
		-- Rule: if typed has multiple raw bytes and typed != key, skip it.
		if #typed > 1 and typed ~= key then
			typed = ""
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
