local v = vim.api
local cmd = vim.cmd
local max = math.max
local min = math.min

local M = {}

---@param token VF.Sequence.Token
---@return boolean
local function enters_modal_state(token)
  return token.kind == "change" or token.kind == "visual"
end

---@param token_text string
---@return string
local function bare_command(token_text)
  return (token_text:gsub("^%d+", ""))
end

local PLAIN_INSERT_ENTRY = {
  i = true, I = true, a = true, A = true,
  o = true, O = true, R = true,
}

---@param token_text string
---@return boolean
local function is_plain_insert_entry(token_text)
  return PLAIN_INSERT_ENTRY[bare_command(token_text)] == true
end

---@param token_text string
---@return string
local function change_mode(token_text)
  return bare_command(token_text):sub(1, 1) == "R" and "R" or "i"
end

--- Precompute replay snapshots by feeding tokens through a hidden probe window.
---@param tokens VF.Sequence.Token[]
---@param lines string[]
---@param row integer 0-indexed
---@param col integer 0-indexed
---@param should_cancel fun(): boolean
---@param on_done fun(states: VF.Replay.Snapshot[])
function M.states(tokens, lines, row, col, should_cancel, on_done)
  local saved_win = v.nvim_get_current_win()

  local buf = v.nvim_create_buf(false, true)
  v.nvim_set_option_value("buftype", "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
  v.nvim_buf_set_lines(buf, 0, -1, true, lines)

  local probe_win = v.nvim_open_win(buf, true, {
    relative  = "editor",
    row       = math.max(0, vim.o.lines - 2),
    col       = math.max(0, vim.o.columns - 2),
    width     = 1,
    height    = 1,
    focusable = true,
    style     = "minimal",
    noautocmd = true,
  })

  local safe_row = max(1, min(row + 1, #lines))
  local line_len = #(lines[safe_row] or "")
  v.nvim_win_set_cursor(probe_win, { safe_row, max(0, min(col, max(0, line_len - 1))) })

  local esc = v.nvim_replace_termcodes("<Esc>", true, false, true)

  local function snap()
    return {
      lines  = v.nvim_buf_get_lines(buf, 0, -1, true),
      cursor = v.nvim_win_get_cursor(probe_win),
      mode   = v.nvim_get_mode().mode,
    }
  end

  ---@type VF.Replay.Snapshot[]
  local states = {}

  ---@param state VF.Replay.Snapshot
  local function apply_probe_snapshot(state)
    if not v.nvim_win_is_valid(probe_win) then return end
    v.nvim_set_current_win(probe_win)
    v.nvim_buf_set_lines(buf, 0, -1, true, state.lines)
    local snap_row = max(1, min(state.cursor[1], #state.lines))
    local line = state.lines[snap_row] or ""
    local safe_col = max(0, min(state.cursor[2], max(0, #line - 1)))
    v.nvim_win_set_cursor(probe_win, { snap_row, safe_col })
  end

  ---@param state VF.Replay.Snapshot
  local function restore_snapshot(state)
    if not v.nvim_win_is_valid(probe_win) then return end
    v.nvim_set_current_win(probe_win)
    pcall(function() cmd("stopinsert") end)
    v.nvim_feedkeys(esc, "nx", false)
    apply_probe_snapshot(state)
  end

  ---@param base VF.Replay.Snapshot
  ---@param seq string
  ---@param mode_override string?
  ---@return VF.Replay.Snapshot
  local function replay_from(base, seq, mode_override)
    restore_snapshot(base)
    v.nvim_feedkeys(v.nvim_replace_termcodes(seq, true, true, true), "nx", false)
    local state = snap()
    if mode_override then state.mode = mode_override end
    return state
  end

  local function teardown()
    if v.nvim_win_is_valid(probe_win) then
      v.nvim_win_close(probe_win, true)
    end
    if v.nvim_buf_is_valid(buf) then
      v.nvim_buf_delete(buf, { force = true })
    end
    if v.nvim_win_is_valid(saved_win) then
      v.nvim_set_current_win(saved_win)
    end
  end

  local co = coroutine.create(function()
    ---@param keys string
    ---@param token VF.Sequence.Token
    local function feed_and_yield(keys, token)
      if not v.nvim_win_is_valid(probe_win) then return end
      v.nvim_set_current_win(probe_win)
      local mode_flags = "n"
      local curr_mode = v.nvim_get_mode().mode
      local first = curr_mode:sub(1, 1)
      local insert_like = first == "i" or first == "R"
      if not insert_like and not enters_modal_state(token) then
        mode_flags = "nx"
      end
      v.nvim_feedkeys(keys, mode_flags, false)
      coroutine.yield()
      coroutine.yield()
    end

    ---@param start_i integer
    ---@return integer next_i
    local function replay_edit_transaction(start_i)
      local base = snap()
      local entry_token = tokens[start_i]
      local parts = { entry_token.text }
      local mode = change_mode(entry_token.text)

      if is_plain_insert_entry(entry_token.text) then
        feed_and_yield(v.nvim_replace_termcodes(entry_token.text, true, true, true),
          entry_token)
        table.insert(states, snap())
      else
        table.insert(states, replay_from(base, table.concat(parts), mode))
      end

      local i = start_i + 1
      while i <= #tokens and tokens[i].kind == "typed" do
        parts[#parts + 1] = tokens[i].text
        table.insert(states, replay_from(base, table.concat(parts), mode))
        i = i + 1
      end

      if i <= #tokens and tokens[i].kind == "escape" then
        parts[#parts + 1] = tokens[i].text
        local final = replay_from(base, table.concat(parts), nil)
        table.insert(states, final)
        coroutine.yield()
        coroutine.yield()
        restore_snapshot(final)
        coroutine.yield()
        coroutine.yield()
        apply_probe_snapshot(final)
        return i + 1
      end

      restore_snapshot(states[#states])
      return i
    end

    table.insert(states, snap())

    local i = 1
    while i <= #tokens do
      if not v.nvim_win_is_valid(probe_win) then return end
      local tok = tokens[i]
      if tok.kind == "change" then
        i = replay_edit_transaction(i)
      else
        feed_and_yield(v.nvim_replace_termcodes(tok.text, true, true, true), tok)
        table.insert(states, snap())
        i = i + 1
      end
    end

    feed_and_yield(esc, { text = "<Esc>", kind = "escape" })
  end)

  local function step()
    if should_cancel() then
      teardown()
      return
    end
    local ok, err = coroutine.resume(co)
    if not ok then
      teardown()
      vim.notify("vimfy precompute failed: " .. tostring(err), vim.log.levels.ERROR)
      return
    end
    if coroutine.status(co) == "dead" then
      teardown()
      on_done(states)
    else
      vim.defer_fn(step, 0)
    end
  end

  step()
end

return M
