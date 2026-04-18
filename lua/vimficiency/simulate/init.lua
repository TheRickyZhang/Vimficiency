-- lua/vimficiency/simulate.lua
local v       = vim.api
local cmd     = vim.cmd
local max     = math.max
local min     = math.min
local ffi_lib = require("vimficiency.ffi")

local M = {}

-- Namespace for cursor highlight extmarks
local cursor_ns = v.nvim_create_namespace("vimficiency_cursor")

-- Highlight groups. `default = true` lets users override without us clobbering
-- their config.
v.nvim_set_hl(0, "VimficiencyReplayCurrent",      { link = "IncSearch", default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursorInsert", { link = "DiffAdd",   default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursorVisual", { link = "Visual",    default = true })

-- =============================================================================
-- Multi-sequence replay state
-- =============================================================================

---@class VimficiencyReplayWin
---@field win integer
---@field buf integer

---@class ReplaySnapshot
---@field lines string[]
---@field cursor [integer, integer]   -- 1-indexed row, 0-indexed col (as nvim returns)
---@field mode string                  -- nvim_get_mode().mode code ("n", "i", "v", "V", "\22", etc.)

---@class VimficiencyReplayState
---@field global_step integer
---@field windows VimficiencyReplayWin[]
---@field sequences string[][]
---@field states ReplaySnapshot[][]   -- states[window_i][step_idx+1]; states[_][1] = step 0
---@field saved_win integer?
---@field saved_tab integer?
---@field sim_tab integer?
---@field precompute_gen integer       -- increments per simulate_compare call to invalidate stale callbacks

---@type VimficiencyReplayState
local multi_sim = {
  global_step = 0,
  windows = {},
  sequences = {},
  states = {},
  saved_win = nil,
  saved_tab = nil,
  sim_tab = nil,
  precompute_gen = 0,
}

---@class VimficiencyFocusState
---@field saved_bufs integer[]
---@field focused_idx integer

---@type VimficiencyFocusState?
local focus_state = nil

-- =============================================================================
-- Tokenization (sequence string → animation steps)
-- =============================================================================

---@type table<string, boolean>
local INSERT_COMMANDS = {
  ["i"] = true, ["I"] = true, ["a"] = true, ["A"] = true,
  ["o"] = true, ["O"] = true, ["s"] = true, ["S"] = true,
  ["R"] = true, ["C"] = true, ["cc"] = true,
}

---@type table<string, boolean>
local NEEDS_FOLLOWING_KEY = {
  ["f"] = true,
  ["F"] = true,
  ["t"] = true,
  ["T"] = true,
  ["r"] = true,
  ["m"] = true,
  ["'"] = true,
  ["`"] = true,
  ["@"] = true,
}

--- Whether a token enters insert mode. Handles standalone insert commands,
--- `c{motion}`, and `c{textobj}` patterns.
---@param token string
---@return boolean
local function is_change_command(token)
  local bare = token:gsub("^%d+", "")  -- strip leading count
  if INSERT_COMMANDS[bare] then return true end
  return bare:sub(1, 1) == "c" and #bare > 1
end

--- Whether a token is an incomplete command that consumes one following key.
--- This matters for oracle precompute: feeding `f` and `;` on separate event
--- loop turns leaves Neovim blocked waiting for the target char, so those must
--- be replayed as a single token `f;`.
---@param token string
---@return boolean
local function needs_following_key(token)
  local bare = token:gsub("^%d+", "")
  return #bare == 1 and NEEDS_FOLLOWING_KEY[bare] == true
end

--- Merge tokens that only become executable once they consume the next key.
---@param tokens string[]
---@return string[]
local function merge_feedable_tokens(tokens)
  ---@type string[]
  local merged = {}
  local i = 1
  while i <= #tokens do
    local token = tokens[i]
    if needs_following_key(token) and i < #tokens then
      merged[#merged + 1] = token .. tokens[i + 1]
      i = i + 2
    else
      merged[#merged + 1] = token
      i = i + 1
    end
  end
  return merged
end

--- Tokenize a sequence for animation, with a character-by-character fallback.
--- Typed text is chunked into 4-char segments for smooth animation.
---@param seq string
---@return string[] tokens
local function tokenize_for_animation(seq)
  local tokens, err = ffi_lib.tokenize_sequence(seq)
  if err or not tokens or #tokens == 0 then
    tokens, err = ffi_lib.tokenize_motions(seq)
    if err or not tokens or #tokens == 0 then
      -- Final fallback: individual chars, keeping <Key> groups intact.
      ---@type string[]
      local chars = {}
      local i = 1
      while i <= #seq do
        if seq:sub(i, i) == "<" then
          local close = seq:find(">", i, true)
          if close then
            table.insert(chars, seq:sub(i, close))
            i = close + 1
          else
            table.insert(chars, seq:sub(i, i))
            i = i + 1
          end
        else
          table.insert(chars, seq:sub(i, i))
          i = i + 1
        end
      end
      return merge_feedable_tokens(chars)
    end
  end

  ---@type string[]
  local expanded = {}
  local in_insert_mode = false
  local CHUNK_SIZE = 4
  for _, token in ipairs(tokens) do
    if token == "<Esc>" then
      table.insert(expanded, token)
      in_insert_mode = false
    elseif in_insert_mode then
      local i = 1
      while i <= #token do
        local chunk_end = min(i + CHUNK_SIZE - 1, #token)
        table.insert(expanded, token:sub(i, chunk_end))
        i = chunk_end + 1
      end
    else
      if is_change_command(token) then in_insert_mode = true end
      table.insert(expanded, token)
    end
  end
  return merge_feedable_tokens(expanded)
end

-- =============================================================================
-- Mode helpers
-- =============================================================================

--- Highlight group for the replay cursor, chosen per simulated mode.
---@param mode string   -- nvim_get_mode().mode code
---@return string
local function mode_hl(mode)
  if mode:sub(1, 1) == "i" then return "VimficiencyReplayCursorInsert" end
  if mode == "v" or mode == "V" or mode == "\22" then
    return "VimficiencyReplayCursorVisual"
  end
  return "Cursor"
end

--- Human-readable replay mode label for the virtual header.
---@param mode string
---@return string
local function mode_label(mode)
  if mode:sub(1, 1) == "i" then return "INSERT" end
  if mode == "v" or mode == "V" or mode == "\22" then return "VISUAL" end
  return "NORMAL"
end

-- =============================================================================
-- Cursor highlight + cleanup
-- =============================================================================

--- Current snapshot for window `i`, clamped to the end of that sequence.
---@param i integer
---@return ReplaySnapshot?
local function current_snap(i)
  local states = multi_sim.states[i]
  if not states or #states == 0 then return nil end
  return states[min(multi_sim.global_step + 1, #states)]
end

---@type fun(): integer
local max_total_steps

--- Wrap highlighted chunks to the available window width.
---@param chunks table[]
---@param width integer
---@return table[]
local function wrap_chunks(chunks, width)
  width = max(1, width)
  ---@type table[]
  local lines = {}
  ---@type table[]
  local line = {}
  local line_width = 0

  local function push_line()
    lines[#lines + 1] = line
    line = {}
    line_width = 0
  end

  for _, chunk in ipairs(chunks) do
    local text = chunk[1]
    local hl = chunk[2]
    local total_chars = vim.fn.strchars(text)
    local start = 0

    while start < total_chars do
      if line_width >= width then
        push_line()
      end

      local piece_chars = 0
      local piece = ""
      while start + piece_chars < total_chars do
        local next_piece = vim.fn.strcharpart(text, start, piece_chars + 1)
        local next_width = vim.fn.strdisplaywidth(next_piece)
        if piece_chars > 0 and line_width + next_width > width then
          break
        end
        piece_chars = piece_chars + 1
        piece = next_piece
        if line_width + next_width >= width then
          break
        end
      end

      if piece_chars == 0 then
        piece_chars = 1
        piece = vim.fn.strcharpart(text, start, 1)
      end

      line[#line + 1] = { piece, hl }
      line_width = line_width + vim.fn.strdisplaywidth(piece)
      start = start + piece_chars
    end
  end

  if #line > 0 or #lines == 0 then
    push_line()
  end
  return lines
end

--- Render multiline virtual header lines above the first buffer line.
---@param i integer
---@param entry VimficiencyReplayWin
---@return nil
local function render_header(i, entry)
  local snap = current_snap(i)
  if not snap then return end

  local total_global = max_total_steps()
  local tokens = multi_sim.sequences[i]
  local local_step = min(multi_sim.global_step, #tokens)
  local width = max(8, v.nvim_win_get_width(entry.win))

  ---@type table[]
  local sequence_chunks = {}
  for j, token in ipairs(tokens) do
    sequence_chunks[#sequence_chunks + 1] = {
      token,
      j == multi_sim.global_step and "VimficiencyReplayCurrent" or "Normal",
    }
  end

  local wrapped_sequence = wrap_chunks(sequence_chunks, width)
  wrapped_sequence[1] = vim.list_extend({
    { "Sequence ", "Comment" },
  }, wrapped_sequence[1])

  ---@type table[]
  local virt_lines = {
    {
      { string.format("[%d] ", i), "Comment" },
      { string.format("Progress %d/%d", multi_sim.global_step, total_global), "Title" },
      { string.format("  Local %d/%d", local_step, #tokens), "Comment" },
    },
    {
      { "Mode ", "Comment" },
      { mode_label(snap.mode), mode_hl(snap.mode) },
    },
  }
  vim.list_extend(virt_lines, wrapped_sequence)

  v.nvim_buf_set_extmark(entry.buf, cursor_ns, 0, 0, {
    virt_lines = virt_lines,
    virt_lines_above = true,
    virt_lines_leftcol = true,
    priority = 2000,
  })

  -- `nvim_win_set_cursor` (called during `apply_state`) auto-scrolls the window
  -- so that `topline = cursor.row` and `topfill = 0`, which hides virt_lines
  -- anchored above line 1. Restore `topfill` to the virt_lines count so the
  -- header stays visible through every step.
  v.nvim_win_call(entry.win, function()
    local view = vim.fn.winsaveview()
    view.topline = 1
    view.topfill = #virt_lines
    vim.fn.winrestview(view)
  end)
end

--- Update cursor highlight extmarks for all windows so the cursor is visible
--- in unfocused buffers too, styled per simulated mode.
local function update_cursor_highlights()
  for i, entry in ipairs(multi_sim.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_clear_namespace(entry.buf, cursor_ns, 0, -1)
      render_header(i, entry)

      local snap = current_snap(i)
      local hl   = snap and mode_hl(snap.mode) or "Cursor"

      local cursor = v.nvim_win_get_cursor(entry.win)
      local row = cursor[1] - 1   -- 0-indexed for extmark
      local col = cursor[2]

      local line = v.nvim_buf_get_lines(entry.buf, row, row + 1, false)[1] or ""
      if col < #line then
        v.nvim_buf_set_extmark(entry.buf, cursor_ns, row, col, {
          end_col = col + 1,
          hl_group = hl,
          priority = 1000,
        })
      elseif #line > 0 then
        v.nvim_buf_set_extmark(entry.buf, cursor_ns, row, #line - 1, {
          end_col = #line,
          hl_group = hl,
          priority = 1000,
        })
      end
    end
  end
end

--- Tear down the replay tab, clear state, return focus to the saved window.
local function cleanup_multi_sim()
  -- Bump the generation so any in-flight precompute callback short-circuits
  -- before touching torn-down state.
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1

  -- In focus mode the non-focused buffers have bufhidden = "hide" and aren't
  -- attached to any window, so `:tabclose` alone would leak them. Delete
  -- everything explicitly; deleting the focused buf also auto-closes the tab.
  if focus_state then
    for _, buf in ipairs(focus_state.saved_bufs) do
      if v.nvim_buf_is_valid(buf) then
        v.nvim_buf_delete(buf, { force = true })
      end
    end
  end

  if multi_sim.sim_tab and v.nvim_tabpage_is_valid(multi_sim.sim_tab) then
    if multi_sim.saved_tab and v.nvim_tabpage_is_valid(multi_sim.saved_tab) then
      v.nvim_set_current_tabpage(multi_sim.saved_tab)
    end
    local sim_tab_nr = v.nvim_tabpage_get_number(multi_sim.sim_tab)
    -- pcall: `tabclose` fails with E444 if sim_tab is the only remaining tab
    -- (can happen when the user's other tabs were closed during replay).
    pcall(function()
      cmd("tabclose " .. sim_tab_nr)
    end)
  end

  if multi_sim.saved_win and v.nvim_win_is_valid(multi_sim.saved_win) then
    v.nvim_set_current_win(multi_sim.saved_win)
  end

  multi_sim.global_step = 0
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.states = {}
  multi_sim.saved_win = nil
  multi_sim.saved_tab = nil
  multi_sim.sim_tab = nil
  focus_state = nil
end

-- =============================================================================
-- Step advancement & winbar rendering
-- =============================================================================

--- Longest sequence length across all buffers.
---@return integer
max_total_steps = function()
  local m = 0
  for _, tokens in ipairs(multi_sim.sequences) do
    if #tokens > m then m = #tokens end
  end
  return m
end

--- Full visual refresh: cursor highlights, virtual headers, screen redraw.
local function refresh()
  update_cursor_highlights()
  cmd("redraw")
end

--- Render one precomputed snapshot into a sim window.
---@param entry VimficiencyReplayWin
---@param snap  ReplaySnapshot
local function apply_state(entry, snap)
  if not (v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf)) then
    return
  end
  v.nvim_buf_set_lines(entry.buf, 0, -1, true, snap.lines)
  -- Clamp cursor just in case the snapshot's row/col exceeds the buffer (e.g.
  -- if the saved position trailed a line that no longer exists).
  local line_count = v.nvim_buf_line_count(entry.buf)
  local row = max(1, min(snap.cursor[1], line_count))
  local line = v.nvim_buf_get_lines(entry.buf, row - 1, row, false)[1] or ""
  -- Neovim snapshots can legitimately sit at col == #line in insert mode, so
  -- preserve end-of-line positions instead of forcing them back onto the last
  -- character.
  local col = max(0, min(snap.cursor[2], #line))
  v.nvim_win_set_cursor(entry.win, { row, col })
end

--- Seek all buffers to the given global step via O(1) snapshot lookup.
---@param target integer
local function seek_to(target)
  target = max(0, min(target, max_total_steps()))
  multi_sim.global_step = target
  for i, entry in ipairs(multi_sim.windows) do
    local states = multi_sim.states[i]
    if states and #states > 0 then
      apply_state(entry, states[min(target + 1, #states)])
    end
  end
  refresh()
end

--- Advance the global step by one.
---@return boolean advanced false when already at the end
local function step_forward()
  if multi_sim.global_step >= max_total_steps() then return false end
  seek_to(multi_sim.global_step + 1)
  return true
end

-- =============================================================================
-- User-facing key handlers
-- =============================================================================

--- `<Right>`: step forward one token.
local function user_step_right()
  step_forward()
end

--- `<Left>`: step backward via snapshot lookup.
local function user_step_left()
  if multi_sim.global_step > 0 then
    seek_to(multi_sim.global_step - 1)
  else
    refresh()
  end
end

-- =============================================================================
-- Buffer-local keymaps (declarative table + echo-hint derivation)
-- =============================================================================

---@class VimficiencyReplayKeymap
---@field lhs string
---@field handler fun()
---@field desc string       -- shown in `:map` listings
---@field group string?     -- short label for the startup echo; entries sharing
---                          a group are merged (e.g. "<Left>/<Right> step").
---                          nil means bound but not advertised.

---@type VimficiencyReplayKeymap[]
local REPLAY_KEYMAPS = {
  { lhs = "<Left>",  handler = user_step_left,    desc = "Step backward", group = "step" },
  { lhs = "<Right>", handler = user_step_right,   desc = "Step forward",  group = "step" },
  { lhs = "q",       handler = cleanup_multi_sim, desc = "Close replay",  group = "quit" },
}

--- Attach every entry in `REPLAY_KEYMAPS` to a sim buffer. `nowait = true`
--- avoids the leader-timeout disambiguation that affects `q` (Vim's macro
--- recording prefix) and is harmless consistency for the other keys.
---@param buf integer
local function attach_replay_keymaps(buf)
  for _, m in ipairs(REPLAY_KEYMAPS) do
    vim.keymap.set("n", m.lhs, m.handler, {
      buffer = buf,
      nowait = true,
      silent = true,
      desc = m.desc,
    })
  end
end

--- Build the `nvim_echo` chunk list for the startup hint by merging entries in
--- `REPLAY_KEYMAPS` that share a `group` (e.g. the two arrow keys both sit in
--- group `"step"` and render as one `<Left>/<Right> step` chunk pair).
---@return table[] chunks
local function build_replay_echo()
  ---@type string[]                  -- group labels in first-seen order
  local order = {}
  ---@type table<string, string[]>   -- group label → ordered lhs values
  local by_group = {}
  for _, m in ipairs(REPLAY_KEYMAPS) do
    if m.group then
      if not by_group[m.group] then
        by_group[m.group] = {}
        table.insert(order, m.group)
      end
      table.insert(by_group[m.group], m.lhs)
    end
  end

  local chunks = { { "vimficiency replay: ", "Title" } }
  for _, group in ipairs(order) do
    table.insert(chunks, { table.concat(by_group[group], "/"), "Special" })
    table.insert(chunks, { " " .. group .. "  ", "Normal" })
  end
  table.insert(chunks, { ":Vimfy focus <N>", "Special" })
  table.insert(chunks, { " / ", "Normal" })
  table.insert(chunks, { ":Vimfy escape", "Special" })
  return chunks
end

-- =============================================================================
-- Precompute (Neovim-as-oracle)
-- =============================================================================

--- Run `tokens` through a hidden probe window seeded with `lines`, snapshotting
--- `{ lines, cursor, mode }` after each token. Async: invokes `on_done(states)`
--- once the sequence has drained and the probe is torn down.
---
--- The probe is a 1×1 off-screen floating window (not `nvim_buf_call`) because
--- `nvim_feedkeys` queues input to drain on the next event-loop tick. At drain
--- time, whatever window is *actually* current is the one that receives input —
--- `nvim_buf_call` only swaps the current buffer synchronously within its
--- callback, so keys queued under `nvim_buf_call` but drained later would leak
--- into the user's real buffer. Keeping the probe as the live current window
--- until the whole sequence is drained avoids that.
---
--- Flag rationale: `nvim_feedkeys(..., "n", false)` — `n` disables remap (we
--- don't want the user's mappings rewriting replay tokens). *No `x` flag*: `x`
--- auto-exits insert/visual like `:normal!`, defeating the mode oracle. The
--- `vim.defer_fn(cb, 0)` after each feed is a yield, not a wait — duration
--- doesn't matter, only that control returns to libuv which drains the input
--- queue before firing our callback.
---
--- Structure: coroutines over callback pyramids. Each token is a `yield`; the
--- driver `step()` resumes after the deferred tick. One `pcall` around the
--- body surfaces any mid-sequence error with a real stack trace instead of
--- silently breaking the chain.
---
---@param tokens string[]
---@param lines string[]
---@param row integer  -- 0-indexed
---@param col integer  -- 0-indexed
---@param should_cancel fun(): boolean
---@param on_done fun(states: ReplaySnapshot[])
local function precompute_states(tokens, lines, row, col, should_cancel, on_done)
  local saved_win = v.nvim_get_current_win()

  local buf = v.nvim_create_buf(false, true)
  v.nvim_set_option_value("buftype",   "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe",   { buf = buf })
  v.nvim_buf_set_lines(buf, 0, -1, true, lines)

  -- 1×1 float positioned at the very bottom-right so it's visually unobtrusive
  -- while still being a real, focusable window — feedkeys needs that.
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

  ---@type ReplaySnapshot[]
  local states = {}

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
    -- Yield helper: feed keys, then yield *twice*. Empirically, a single
    -- defer tick is enough for the input queue to drain, but `nvim_get_mode()`
    -- sometimes lags one more tick behind a mode transition (observed reliably
    -- for `i` → `<Esc>` with only one intermediate feed). Two yields makes the
    -- mode query stable across all mode transitions we care about. Cost: 2×
    -- event-loop ticks per token — still imperceptible for typical replays.
    local function feed_and_yield(keys)
      if not v.nvim_win_is_valid(probe_win) then return end
      v.nvim_set_current_win(probe_win)
      v.nvim_feedkeys(keys, "n", false)
      coroutine.yield()
      coroutine.yield()
    end

    -- No prelude <Esc>: opening the probe float via `nvim_open_win(_, true, ...)`
    -- makes it the current window, and a window switch naturally exits insert
    -- mode. Pre-feeding `<Esc>` in the already-normal state was empirically
    -- introducing an off-by-one in the first post-feed mode snapshot.
    table.insert(states, snap())

    for _, token in ipairs(tokens) do
      -- If the probe window died underneath us (rare but possible if an
      -- autocmd or concurrent simulate_compare bulldozed it), bail cleanly.
      if not v.nvim_win_is_valid(probe_win) then return end
      feed_and_yield(v.nvim_replace_termcodes(token, true, false, true))
      table.insert(states, snap())
    end

    -- Flush residual mode so nothing bleeds into the user's session when the
    -- sequence ended mid-insert or mid-visual (no trailing `<Esc>` in tokens).
    feed_and_yield(esc)
  end)

  local function step()
    if should_cancel() then
      teardown()
      return
    end
    local ok, err = coroutine.resume(co)
    if not ok then
      teardown()
      vim.notify("vimficiency precompute failed: " .. tostring(err),
        vim.log.levels.ERROR)
      return
    end
    if coroutine.status(co) == "dead" then
      teardown()
      on_done(states)
    else
      vim.defer_fn(step, 0)  -- yield to libuv; duration doesn't matter
    end
  end

  step()
end

-- =============================================================================
-- Buffer / window setup
-- =============================================================================

--- Create a scratch buffer for one replay sequence and attach keymaps.
---@param lines string[]
---@param label string
---@return integer buf
local function create_sim_buffer(lines, label)
  local buf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_name(buf, "vimficiency-sim-" .. label:gsub("%s+", "_"):sub(1, 20))
  v.nvim_set_option_value("buftype", "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
  v.nvim_set_option_value("swapfile", false, { buf = buf })
  v.nvim_set_option_value("modifiable", true, { buf = buf })
  v.nvim_buf_set_lines(buf, 0, -1, true, lines)

  attach_replay_keymaps(buf)

  return buf
end

--- Apply the cursor/winbar decorations a sim window needs. Shared between the
--- initial layout (`setup_sim_window`) and the post-focus rebuild (`user_escape`).
---@param win integer
local function decorate_sim_window(win)
  v.nvim_set_option_value("cursorline", true, { win = win })
  v.nvim_set_option_value("cursorcolumn", true, { win = win })
  v.nvim_set_option_value("winbar", "", { win = win })
end

--- Attach a buffer to a window, clamp the cursor, enable focus indicators.
---@param win integer
---@param buf integer
---@param lines string[]
---@param row integer 0-indexed
---@param col integer 0-indexed
---@param label string
local function setup_sim_window(win, buf, lines, row, col, label)
  v.nvim_win_set_buf(win, buf)

  local line_count = #lines
  local safe_row = max(1, min(row + 1, line_count))
  local line_len = #(lines[safe_row] or "")
  local safe_col = max(0, min(col, max(0, line_len - 1)))
  v.nvim_win_set_cursor(win, { safe_row, safe_col })

  decorate_sim_window(win)
end

--- Open the sim tab, create windows/buffers, attach keymaps, fire the hint echo.
--- Runs after all precomputes have completed.
---@param lines string[]
---@param row integer
---@param col integer
---@param sequences string[]
---@param tokenized string[][]
local function build_sim_ui(lines, row, col, sequences, tokenized)
  cmd("tabnew")
  multi_sim.sim_tab = v.nvim_get_current_tabpage()
  local tabnew_buf = v.nvim_get_current_buf()

  for i, seq in ipairs(sequences) do
    -- `multi_sim.sequences[i]` was set eagerly in `simulate_compare`, so
    -- no assignment needed here.
    local label = string.format("[%d] %s", i, seq)
    local buf = create_sim_buffer(lines, label)

    ---@type integer
    local win
    if i == 1 then
      win = v.nvim_get_current_win()
    else
      cmd("vsplit")
      win = v.nvim_get_current_win()
    end

    setup_sim_window(win, buf, lines, row, col, label)
    ---@type VimficiencyReplayWin
    local replay_win = { win = win, buf = buf }
    table.insert(multi_sim.windows, replay_win)
  end

  -- tabnew's empty buffer is no longer displayed once we swap in the sim
  -- buffers above, so drop it.
  if v.nvim_buf_is_valid(tabnew_buf) then
    v.nvim_buf_delete(tabnew_buf, { force = true })
  end

  cmd("wincmd =")

  if #multi_sim.windows > 0 and v.nvim_win_is_valid(multi_sim.windows[1].win) then
    v.nvim_set_current_win(multi_sim.windows[1].win)
  end

  -- Render the initial step-0 state in each window, then refresh highlights/winbars.
  for i, entry in ipairs(multi_sim.windows) do
    local states = multi_sim.states[i]
    if states and #states > 0 then
      apply_state(entry, states[1])
    end
  end
  refresh()

  vim.schedule(function()
    v.nvim_echo(build_replay_echo(), false, {})
  end)
end

-- =============================================================================
-- Focus / escape (command handlers)
-- =============================================================================

--- `:Vimfy focus <N>`: collapse to a single window showing the Nth sim buffer
--- full-screen. Stepping still works; other buffers stay alive (bufhidden =
--- "hide") so `:Vimfy escape` can restore the side-by-side layout.
---@param idx integer
local function user_focus(idx)
  if focus_state then
    vim.notify("vimficiency: already focused; use :Vimfy escape first",
      vim.log.levels.WARN)
    return
  end
  if #multi_sim.windows == 0 then
    vim.notify("vimficiency: no replay in progress", vim.log.levels.WARN)
    return
  end
  if not idx or idx < 1 or idx > #multi_sim.windows then
    vim.notify(
      "vimficiency: focus index must be 1.." .. #multi_sim.windows,
      vim.log.levels.ERROR)
    return
  end

  ---@type integer[]
  local saved_bufs = {}
  for i, entry in ipairs(multi_sim.windows) do
    saved_bufs[i] = entry.buf
    -- Prevent `:only` from wiping the non-focused buffers.
    v.nvim_set_option_value("bufhidden", "hide", { buf = entry.buf })
  end

  local focus_win = multi_sim.windows[idx].win
  v.nvim_set_current_win(focus_win)
  cmd("only")

  multi_sim.windows = { { win = focus_win, buf = saved_bufs[idx] } }
  focus_state = { saved_bufs = saved_bufs, focused_idx = idx }
  refresh()
end

--- `:Vimfy escape`: restore the side-by-side replay layout and re-sync every
--- buffer to the current global_step (non-focused buffers were frozen during
--- focus mode).
local function user_escape()
  if not focus_state then
    vim.notify("vimficiency: not currently focused", vim.log.levels.INFO)
    return
  end
  local saved_bufs = focus_state.saved_bufs
  local focused_idx = focus_state.focused_idx

  -- The current window becomes slot 1; vsplit clones leftward for each
  -- subsequent buffer, matching the original layout built in `build_sim_ui`.
  local current_win = v.nvim_get_current_win()
  v.nvim_win_set_buf(current_win, saved_bufs[1])
  decorate_sim_window(current_win)

  ---@type VimficiencyReplayWin[]
  local new_windows = { { win = current_win, buf = saved_bufs[1] } }
  for i = 2, #saved_bufs do
    cmd("vsplit")
    local new_win = v.nvim_get_current_win()
    v.nvim_win_set_buf(new_win, saved_bufs[i])
    decorate_sim_window(new_win)
    new_windows[i] = { win = new_win, buf = saved_bufs[i] }
  end

  -- Re-arm bufhidden=wipe so `q` (tabclose) reclaims the buffers.
  for _, buf in ipairs(saved_bufs) do
    if v.nvim_buf_is_valid(buf) then
      v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
    end
  end

  cmd("wincmd =")
  v.nvim_set_current_win(new_windows[focused_idx].win)

  multi_sim.windows = new_windows
  focus_state = nil
  -- Re-apply snapshots across the board: non-focused buffers were frozen
  -- while in focus mode and need to catch up to global_step.
  seek_to(multi_sim.global_step)
end

-- =============================================================================
-- Public API
-- =============================================================================

--- Simulate multiple motion sequences side-by-side in a new tab. Arrow keys
--- step manually; `q` quits; `:Vimfy focus <N>` / `:Vimfy escape` toggle the
--- single-buffer view.
---
--- Before opening the sim tab, this precomputes `{ lines, cursor, mode }`
--- snapshots per token by driving Neovim itself as the oracle (via
--- `nvim_feedkeys` into a hidden buffer). Per-mode cursor styling and the
--- `-- INSERT --` / `-- VISUAL --` winbar tag come from those snapshots.
---
---@param lines string[] Buffer content to simulate on
---@param row integer 0-indexed starting row
---@param col integer 0-indexed starting column
---@param sequences string[] Array of motion sequences (e.g., {"3w", "wwwfa;", "jjjw"})
function M.simulate_compare(lines, row, col, sequences)
  if #multi_sim.windows > 0 then
    cleanup_multi_sim()
  end
  if not sequences or #sequences == 0 then
    vim.notify("simulate_compare: no sequences provided", vim.log.levels.ERROR)
    return
  end
  if not lines or #lines == 0 then
    vim.notify("simulate_compare: no lines provided", vim.log.levels.ERROR)
    return
  end

  multi_sim.global_step = 0
  multi_sim.saved_win = v.nvim_get_current_win()
  multi_sim.saved_tab = v.nvim_get_current_tabpage()
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.states = {}
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  local my_gen = multi_sim.precompute_gen

  ---@type string[][]
  local tokenized = {}
  for i, seq in ipairs(sequences) do
    tokenized[i] = tokenize_for_animation(seq)
    -- Populate sequences eagerly so things that consult them (winbar
    -- rendering, tests probing the tokenization) work before the async
    -- precompute has built the sim UI.
    multi_sim.sequences[i] = tokenized[i]
  end

  vim.notify("vimficiency: precomputing replay…", vim.log.levels.INFO)

  local function precompute_next(idx)
    if multi_sim.precompute_gen ~= my_gen then
      -- A newer simulate_compare superseded us; abandon quietly.
      return
    end
    if idx > #sequences then
      build_sim_ui(lines, row, col, sequences, tokenized)
      return
    end
    precompute_states(tokenized[idx], lines, row, col, function()
      return multi_sim.precompute_gen ~= my_gen
    end, function(states)
      if multi_sim.precompute_gen ~= my_gen then return end
      multi_sim.states[idx] = states
      precompute_next(idx + 1)
    end)
  end

  precompute_next(1)
end

M.cleanup_compare = cleanup_multi_sim
M.focus           = user_focus
M.escape          = user_escape

-- Debug-only accessors (not part of the public API). Let tests peek at
-- internal state without parsing winbars or tabs.
M._debug_get_states = function() return multi_sim.states end
M._debug_get_sequences = function() return multi_sim.sequences end
M._debug_get_windows = function() return multi_sim.windows end
M._debug_seek_to = seek_to
M._debug_tokenize_for_animation = tokenize_for_animation

return M
