-- lua/vimficiency/simulate.lua
local v       = vim.api
local cmd     = vim.cmd
local max     = math.max
local min     = math.min
local ffi_lib = require("vimficiency.ffi")
local util    = require("vimficiency.util")

local M = {}

local cursor_ns = v.nvim_create_namespace("vimficiency_cursor")

-- `default = true` lets users override these groups.
v.nvim_set_hl(0, "VimficiencyReplayCurrent",      { link = "IncSearch", default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursor",       { link = "Search",    default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursorInsert", { link = "DiffAdd",   default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursorVisual", { link = "Visual",    default = true })
v.nvim_set_hl(0, "VimficiencyReplayActive",       { link = "Title",     default = true })

-- =============================================================================
-- Multi-sequence replay state
-- =============================================================================

---@class VimficiencyReplayWin
---@field win integer
---@field buf integer
---@field seq_idx integer   -- Index into `multi_sim.sequences` / `.states` / `.costs`.

---@class ReplaySnapshot
---@field lines string[]
---@field cursor [integer, integer]   -- 1-indexed row, 0-indexed col (as nvim returns)
---@field mode string                  -- nvim_get_mode().mode code ("n", "i", "v", "V", "\22", etc.)

---@class VimficiencyReplayItem
---@field seq string                   -- motion sequence to simulate
---@field cost string?                 -- optional pre-formatted cost for the per-buffer header

---@class VimficiencyReplayState
---@field global_step integer
---@field windows VimficiencyReplayWin[]
---@field sequences VimficiencyToken[][]
---@field costs (string?)[]            -- parallel to sequences; nil = no cost row
---@field states ReplaySnapshot[][]    -- states[window_i][1] is the initial snapshot
---@field saved_win integer?
---@field saved_tab integer?
---@field sim_tab integer?
---@field replay_label string?         -- display label for the replay statusline (typically the session alias)
---@field start_row integer?           -- 0-indexed start row (from `simulate_compare`)
---@field start_col integer?           -- 0-indexed start col
---@field end_row integer?             -- 0-indexed end row (from `opts.end_row`; nil = unknown)
---@field end_col integer?             -- 0-indexed end col
---@field status_win integer?          -- bottom 1-line split showing the replay statusline
---@field status_buf integer?
---@field precompute_gen integer       -- increments per simulate_compare call to invalidate stale callbacks

---@type VimficiencyReplayState
local multi_sim = {
  global_step = 0,
  windows = {},
  sequences = {},
  costs = {},
  states = {},
  saved_win = nil,
  saved_tab = nil,
  sim_tab = nil,
  replay_label = nil,
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
--
-- Tokens carry a `kind` tagged by the C++ parser — see `VimficiencyToken` in
-- `ffi.lua`. The historical Lua-side classifier tables (INSERT_COMMANDS,
-- VISUAL_ENTER_COMMANDS, NEEDS_FOLLOWING_KEY) are gone: they were a parallel
-- source of truth to the parser's grammar and drifted silently. See
-- `dev/lua/replay-precompute.md` for the gating semantics.

--- Whether a token enters Insert/Visual from Normal. Consulted by the
--- precompute coroutine to decide between the synchronous-drain (`nx`) and
--- yield fast paths — modal transitions must stay on the yield path so the
--- oracle can sample the intermediate modal state.
---@param token VimficiencyToken
---@return boolean
local function enters_modal_state(token)
  return token.kind == "change" or token.kind == "visual"
end

-- Fallback-only classifier for the char-by-char split path, which runs
-- when both C++ tokenizers fail (e.g., truly malformed sequences the
-- grammar can't parse). Much smaller than the old classifier set because
-- C++ now handles visual-mode entry; this covers the residual cases and
-- defaults non-matches to "motion" (fast-path-eligible, safe).
local FALLBACK_FOLLOW_BARE = {
  f = true, F = true, t = true, T = true, r = true,
  m = true, ["'"] = true, ["`"] = true, ["@"] = true,
}
local FALLBACK_INSERT_BARE = {
  i = true, I = true, a = true, A = true, o = true, O = true,
  s = true, S = true, R = true, C = true, cc = true,
}
local FALLBACK_VISUAL_BARE = {
  v = true, V = true, ["<C-v>"] = true, gh = true, gH = true,
}

---@param text string
---@return "motion"|"change"|"visual"
local function classify_fallback(text)
  local bare = text:gsub("^%d+", "")
  if FALLBACK_INSERT_BARE[bare] then return "change" end
  if FALLBACK_VISUAL_BARE[bare] then return "visual" end
  -- `c{motion}` / `c{textobj}` — any `c`-prefixed token of length >1 that
  -- isn't in the bare insert table. (`cc` matched above.)
  if bare:sub(1, 1) == "c" and #bare > 1 then return "change" end
  return "motion"
end

--- Tokenize a sequence for animation. Returns kinded tokens — consumers
--- (the precompute coroutine, header rendering, debug dump) should use
--- `.text` when feeding nvim and `.kind` when gating modal behavior.
---@param seq string
---@return VimficiencyToken[] tokens
local function tokenize_for_animation(seq)
  local tokens, err = ffi_lib.tokenize_sequence(seq)
  if err or not tokens or #tokens == 0 then
    tokens, err = ffi_lib.tokenize_motions(seq)
    if err or not tokens or #tokens == 0 then
      -- Final fallback: individual chars, keeping `<Key>` groups intact.
      -- We split the raw sequence char-by-char, then merge tokens that
      -- consume the next key (fx, rY, etc.), then tag each with a kind
      -- via the minimal `classify_fallback`. C++ grammar failures only
      -- reach this branch for truly malformed input now that visual is
      -- handled upstream.
      ---@type string[]
      local chars = {}
      local i = 1
      while i <= #seq do
        if seq:sub(i, i) == "<" then
          local close = seq:find(">", i, true)
          if close then
            chars[#chars + 1] = seq:sub(i, close)
            i = close + 1
          else
            chars[#chars + 1] = seq:sub(i, i)
            i = i + 1
          end
        else
          chars[#chars + 1] = seq:sub(i, i)
          i = i + 1
        end
      end
      ---@type VimficiencyToken[]
      local fallback = {}
      local k = 1
      while k <= #chars do
        local text = chars[k]
        local bare = text:gsub("^%d+", "")
        if #bare == 1 and FALLBACK_FOLLOW_BARE[bare] and k < #chars then
          text = text .. chars[k + 1]
          k = k + 2
        else
          k = k + 1
        end
        fallback[#fallback + 1] = { text = text, kind = classify_fallback(text) }
      end
      return fallback
    end
  end

  -- Happy path: tokens already carry `.kind` from the C++ parser. Chunk
  -- any `typed` tokens into small pieces so the animation shows typed
  -- text materializing gradually, not as one jump.
  ---@type VimficiencyToken[]
  local expanded = {}
  local CHUNK_SIZE = 4
  for _, tok in ipairs(tokens) do
    if tok.kind == "typed" then
      local i = 1
      while i <= #tok.text do
        local chunk_end = min(i + CHUNK_SIZE - 1, #tok.text)
        expanded[#expanded + 1] = {
          text = tok.text:sub(i, chunk_end),
          kind = "typed",
        }
        i = chunk_end + 1
      end
    else
      expanded[#expanded + 1] = tok
    end
  end
  return expanded
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
  return "VimficiencyReplayCursor"
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

--- Render the virtual header above a replay buffer.
---@param seq_idx integer
---@param entry VimficiencyReplayWin
---@return nil
local function render_header(seq_idx, entry)
  local snap = current_snap(seq_idx)
  if not snap then return end

  local tokens = multi_sim.sequences[seq_idx]
  local local_step = min(multi_sim.global_step, #tokens)
  local width = max(8, v.nvim_win_get_width(entry.win))

  ---@type table[]
  local sequence_chunks = {}
  for j, tok in ipairs(tokens) do
    sequence_chunks[#sequence_chunks + 1] = {
      tok.text,
      j == multi_sim.global_step and "VimficiencyReplayCurrent" or "Normal",
    }
  end

  local wrapped_sequence = wrap_chunks(sequence_chunks, width)
  wrapped_sequence[1] = vim.list_extend({
    { "Sequence ", "Comment" },
  }, wrapped_sequence[1])

  local is_active = entry.win == v.nvim_get_current_win()
  local label_hl = is_active and "VimficiencyReplayActive" or "Comment"

  ---@type table[]
  local virt_lines = {
    { { "", "Normal" } },
    {
      { string.format("[%d] ", seq_idx), label_hl },
      { string.format("Local %d/%d", local_step, #tokens), "Comment" },
    },
    {
      { "Mode ", "Comment" },
      { mode_label(snap.mode), "Normal" },
    },
  }
  if multi_sim.costs[seq_idx] then
    virt_lines[#virt_lines + 1] = {
      { "Cost ", "Comment" },
      { multi_sim.costs[seq_idx], "Normal" },
    }
  end
  vim.list_extend(virt_lines, wrapped_sequence)
  virt_lines[#virt_lines + 1] = { { "", "Normal" } }

  v.nvim_buf_set_extmark(entry.buf, cursor_ns, 0, 0, {
    virt_lines = virt_lines,
    virt_lines_above = true,
    virt_lines_leftcol = true,
    priority = 2000,
  })

  -- Keep the virtual header visible after `apply_state()` moves the cursor.
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
  for _, entry in ipairs(multi_sim.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_clear_namespace(entry.buf, cursor_ns, 0, -1)
      render_header(entry.seq_idx, entry)

      local snap = current_snap(entry.seq_idx)
      local hl   = snap and mode_hl(snap.mode) or "VimficiencyReplayCursor"

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

-- Forward-declared for cleanup.
---@type fun()
local destroy_status_bar

--- Tear down the replay tab, clear state, return focus to the saved window.
local function cleanup_multi_sim()
  -- Cancel any in-flight precompute callbacks.
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1

  destroy_status_bar()

  -- Focus mode leaves hidden buffers behind; delete them explicitly.
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
    -- `tabclose` fails with E444 if this is the last tab.
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
  multi_sim.costs = {}
  multi_sim.states = {}
  multi_sim.saved_win = nil
  multi_sim.saved_tab = nil
  multi_sim.sim_tab = nil
  multi_sim.replay_label = nil
  multi_sim.start_row = nil
  multi_sim.start_col = nil
  multi_sim.end_row = nil
  multi_sim.end_col = nil
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

--- Build the shared replay statusline text.
---@return string
local function build_statusline()
  local label = multi_sim.replay_label
  local label_part = (label and label ~= "") and ("  " .. label) or ""
  local pos_part = ""
  if multi_sim.start_row and multi_sim.end_row then
    pos_part = string.format("  (%d:%d → %d:%d)",
      multi_sim.start_row + 1, multi_sim.start_col + 1,
      multi_sim.end_row + 1, multi_sim.end_col + 1)
  end
  local text = string.format(" vimficiency%s  step %d/%d%s ",
    label_part, multi_sim.global_step, max_total_steps(), pos_part)
  -- Escape `%` so labels cannot be parsed as statusline directives.
  return (text:gsub("%%", "%%%%"))
end

--- Create a dedicated status bar window for global replay state.
local function create_status_bar()
  cmd("botright 1split")
  local win = v.nvim_get_current_win()
  local buf = v.nvim_create_buf(false, true)
  v.nvim_set_option_value("buftype",   "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe",   { buf = buf })
  v.nvim_win_set_buf(win, buf)

  v.nvim_set_option_value("winfixheight",   true,  { win = win })
  v.nvim_set_option_value("number",         false, { win = win })
  v.nvim_set_option_value("relativenumber", false, { win = win })
  v.nvim_set_option_value("signcolumn",     "no",  { win = win })
  v.nvim_set_option_value("cursorline",     false, { win = win })
  v.nvim_set_option_value("cursorcolumn",   false, { win = win })
  v.nvim_set_option_value("statusline",     " ",   { win = win })
  v.nvim_set_option_value("winhighlight",
    "Normal:StatusLine,StatusLine:Normal,StatusLineNC:Normal", { win = win })

  multi_sim.status_win = win
  multi_sim.status_buf = buf
end

local function update_status_bar()
  if not multi_sim.status_win or not v.nvim_win_is_valid(multi_sim.status_win) then
    return
  end
  v.nvim_buf_set_lines(multi_sim.status_buf, 0, -1, true,
    { build_statusline() })
end

-- Assigned to the forward declaration above.
---@diagnostic disable-next-line: redefined-local
destroy_status_bar = function()
  if multi_sim.status_win and v.nvim_win_is_valid(multi_sim.status_win) then
    v.nvim_win_close(multi_sim.status_win, true)
  end
  if multi_sim.status_buf and v.nvim_buf_is_valid(multi_sim.status_buf) then
    v.nvim_buf_delete(multi_sim.status_buf, { force = true })
  end
  multi_sim.status_win = nil
  multi_sim.status_buf = nil
end

--- Refresh headers, cursor highlights, and the status bar.
local function refresh()
  update_cursor_highlights()
  update_status_bar()
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
  -- Clamp saved cursors against the current buffer.
  local line_count = v.nvim_buf_line_count(entry.buf)
  local row = max(1, min(snap.cursor[1], line_count))
  local line = v.nvim_buf_get_lines(entry.buf, row - 1, row, false)[1] or ""
  -- Insert-mode snapshots may legally sit at `col == #line`.
  local col = max(0, min(snap.cursor[2], #line))
  v.nvim_win_set_cursor(entry.win, { row, col })
end

--- Seek all buffers to the given global step via O(1) snapshot lookup.
---@param target integer
local function seek_to(target)
  target = max(0, min(target, max_total_steps()))
  multi_sim.global_step = target
  for _, entry in ipairs(multi_sim.windows) do
    local states = multi_sim.states[entry.seq_idx]
    if states and #states > 0 then
      apply_state(entry, states[min(target + 1, #states)])
    end
  end
  refresh()
end

--- Upper bound for forward stepping in the current view.
---@return integer
local function step_cap()
  if focus_state then
    local focused_tokens = multi_sim.sequences[focus_state.focused_idx]
    if focused_tokens then return #focused_tokens end
  end
  return max_total_steps()
end

--- Advance the global step by one.
---@return boolean advanced false when already at the cap for the current view
local function step_forward()
  if multi_sim.global_step >= step_cap() then return false end
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

--- `<Left>`: step backward.
local function user_step_left()
  if multi_sim.global_step > 0 then
    seek_to(multi_sim.global_step - 1)
  else
    refresh()
  end
end

-- Forward-declared for the focus toggle handler.
---@type fun(idx: integer)
local user_focus
---@type fun()
local user_escape

--- Cycle to the next or previous replay sequence.
---@param step integer   -- `+1` for next, `-1` for prev
local function user_cycle(step)
  if focus_state then
    local n = #focus_state.saved_bufs
    if n < 2 then return end
    local new_idx = ((focus_state.focused_idx - 1 + step) % n + n) % n + 1
    local entry = multi_sim.windows[1]
    local new_buf = focus_state.saved_bufs[new_idx]
    v.nvim_win_set_buf(entry.win, new_buf)
    entry.buf = new_buf
    entry.seq_idx = new_idx
    focus_state.focused_idx = new_idx
    seek_to(multi_sim.global_step)
    return
  end

  if #multi_sim.windows < 2 then return end
  local cur_win = v.nvim_get_current_win()
  for i, e in ipairs(multi_sim.windows) do
    if e.win == cur_win then
      local n = #multi_sim.windows
      local nxt = multi_sim.windows[((i - 1 + step) % n + n) % n + 1]
      if v.nvim_win_is_valid(nxt.win) then
        v.nvim_set_current_win(nxt.win)
      end
      return
    end
  end
end

local function user_cycle_next() user_cycle(1) end
local function user_cycle_prev() user_cycle(-1) end

--- Copy the current window's sequence to the unnamed and `+` registers.
local function user_yank_sequence()
  local cur_win = v.nvim_get_current_win()
  for _, entry in ipairs(multi_sim.windows) do
    if entry.win == cur_win then
      local tokens = multi_sim.sequences[entry.seq_idx]
      if not tokens then return end
      ---@type string[]
      local parts = {}
      for _, tok in ipairs(tokens) do parts[#parts + 1] = tok.text end
      local seq = table.concat(parts, "")
      vim.fn.setreg('"', seq)
      vim.fn.setreg('+', seq)
      vim.notify("vimficiency: yanked [" .. entry.seq_idx .. "] " .. seq,
        vim.log.levels.INFO)
      return
    end
  end
end

--- Dump replay state to `:messages`.
--- See `dev/lua/replay-precompute.md` for the trace fields.
local function user_debug_dump()
  ---@type string[]
  local out = {}
  local function pr(s) out[#out + 1] = s end

  pr("=== vimficiency replay debug ===")
  pr(string.format("global_step = %d (max = %d)",
    multi_sim.global_step, max_total_steps()))
  pr(string.format("focus_state = %s",
    focus_state and vim.inspect(focus_state) or "nil"))
  pr(string.format("current_win = %d", v.nvim_get_current_win()))

  for i, entry in ipairs(multi_sim.windows) do
    local states = multi_sim.states[entry.seq_idx] or {}
    local tokens = multi_sim.sequences[entry.seq_idx] or {}
    local snap_idx = min(multi_sim.global_step + 1, #states)
    local snap = states[snap_idx]
    local initial = states[1]
    local rendered = v.nvim_win_is_valid(entry.win)
        and v.nvim_win_get_cursor(entry.win)
        or { -1, -1 }
    local live_lines = v.nvim_buf_is_valid(entry.buf)
        and v.nvim_buf_get_lines(entry.buf, 0, -1, true)
        or {}
    local active_token = tokens[multi_sim.global_step]

    pr(string.format(
      "window[%d]: win=%d buf=%d seq_idx=%d #tokens=%d #states=%d",
      i, entry.win, entry.buf, entry.seq_idx, #tokens, #states))
    pr(string.format("  active token = %s (%s)  (global_step = %d)",
      active_token and string.format("%q", active_token.text) or "nil",
      active_token and active_token.kind or "-",
      multi_sim.global_step))
    pr(string.format("  rendered cursor = (%d,%d)", rendered[1], rendered[2]))
    if initial then
      pr(string.format(
        "  snapshot[1]  (initial) = cursor=(%d,%d) mode=%s",
        initial.cursor[1], initial.cursor[2], initial.mode))
      pr(string.format("    initial line[%d] = %s",
        initial.cursor[1],
        vim.inspect(initial.lines[initial.cursor[1]] or "")))
    else
      pr("  snapshot[1]  (initial) = MISSING")
    end
    if snap then
      pr(string.format(
        "  snapshot[%d]          = cursor=(%d,%d) mode=%s",
        snap_idx, snap.cursor[1], snap.cursor[2], snap.mode))
      pr(string.format("    snapshot line[%d] = %s",
        snap.cursor[1], vim.inspect(snap.lines[snap.cursor[1]] or "")))
    else
      pr(string.format("  snapshot[%d]          = MISSING", snap_idx))
    end
    pr(string.format("    live    line[%d] = %s",
      rendered[1], vim.inspect(live_lines[rendered[1]] or "")))
    pr(string.format("  tokens = %s", vim.inspect(tokens)))
  end
  pr("=== end ===")

  local msg = table.concat(out, "\n")
  vim.notify("vimficiency: debug dump written to :messages", vim.log.levels.INFO)
  v.nvim_echo({ { msg, "Normal" } }, true, {})
end

--- Focus the current replay window, or escape back to split layout.
local function user_toggle_focus()
  if focus_state then
    user_escape()
    return
  end
  local cur_win = v.nvim_get_current_win()
  for idx, entry in ipairs(multi_sim.windows) do
    if entry.win == cur_win then
      user_focus(idx)
      return
    end
  end
  vim.notify("vimficiency: cursor is not in a replay window",
    vim.log.levels.WARN)
end

-- =============================================================================
-- Buffer-local keymaps (declarative table + echo-hint derivation)
-- =============================================================================

---@class VimficiencyReplayKeymap
---@field lhs string
---@field handler fun()
---@field desc string       -- shown in `:map` listings
---@field group string?     -- Startup-echo label; nil means "do not advertise".

---@type VimficiencyReplayKeymap[]
local REPLAY_KEYMAPS = util.with_help_keymaps({
  { lhs = "<Left>",  handler = user_step_left,    desc = "Step backward",                  group = "step"  },
  { lhs = "<Tab>",   handler = user_cycle_next,   desc = "Cycle to next sim sequence",     group = "cycle" },
  { lhs = "<S-Tab>", handler = user_cycle_prev,   desc = "Cycle to prev sim sequence",     group = "cycle" },
  { lhs = "<CR>",    handler = user_toggle_focus, desc = "Focus / unfocus current buffer", group = "focus" },
  { lhs = "<Right>",   handler = user_step_right,    desc = "Step forward",                    group = "step"  },
  { lhs = "<leader>y", handler = user_yank_sequence, desc = "Yank this window's sequence",     group = "yank"  },
  { lhs = "D",         handler = user_debug_dump,    desc = "Dump replay state to :messages" },
  { lhs = "q",         handler = cleanup_multi_sim,  desc = "Close replay",                    group = "quit"  },
}, "Vimficiency Replay Keys", "vimficiency-inspecting-results-replay-buffer-keys")

--- Attach replay keymaps to a buffer.
---@param buf integer
local function attach_replay_keymaps(buf)
  util.set_buffer_keymaps(buf, REPLAY_KEYMAPS)
end

-- =============================================================================
-- Precompute (Neovim-as-oracle)
-- =============================================================================

--- Precompute replay snapshots by feeding tokens through a hidden probe window.
--- See `dev/lua/replay-precompute.md` for the event-loop and mode details.
---@param tokens VimficiencyToken[]
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

  -- Use a real window so `nvim_feedkeys()` lands in the probe.
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
    -- Yield twice so `nvim_get_mode()` catches up after modal transitions.
    ---@param keys string
    ---@param token VimficiencyToken
    local function feed_and_yield(keys, token)
      if not v.nvim_win_is_valid(probe_win) then return end
      v.nvim_set_current_win(probe_win)
      local mode_flags = "n"
      local curr_mode = v.nvim_get_mode().mode
      if curr_mode:sub(1, 1) == "n" and not enters_modal_state(token) then
        mode_flags = "nx"
      end
      v.nvim_feedkeys(keys, mode_flags, false)
      coroutine.yield()
      coroutine.yield()
    end

    table.insert(states, snap())

    for _, tok in ipairs(tokens) do
      if not v.nvim_win_is_valid(probe_win) then return end
      feed_and_yield(v.nvim_replace_termcodes(tok.text, true, false, true), tok)
      table.insert(states, snap())
    end

    -- Flush any remaining modal state before teardown. Synthesize an
    -- escape-kinded token so `feed_and_yield` routes it correctly (kind
    -- is not modal-entering → fast path from Normal, yield path otherwise).
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
      vim.notify("vimficiency precompute failed: " .. tostring(err),
        vim.log.levels.ERROR)
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

-- =============================================================================
-- Buffer / window setup
-- =============================================================================

--- Create a scratch buffer for one replay sequence.
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

  -- Refresh the active-window marker on window changes.
  v.nvim_create_autocmd("WinEnter", {
    buffer = buf,
    callback = function() update_cursor_highlights() end,
    desc = "vimficiency: refresh focus indicator on window enter",
  })

  return buf
end

--- Apply replay window-local options.
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

--- Build the replay tab after precompute completes.
---@param lines string[]
---@param row integer
---@param col integer
---@param items VimficiencyReplayItem[]
local function build_sim_ui(lines, row, col, items)
  cmd("tabnew")
  multi_sim.sim_tab = v.nvim_get_current_tabpage()
  local tabnew_buf = v.nvim_get_current_buf()

  -- Layout: up to 4 windows across per row, wrapping to a second row at
  -- item 5. `botright split` creates a full-width bottom row regardless
  -- of where the current window sits, so vsplits within it stay
  -- confined to that row.
  local ROW_CAPACITY = 4
  for i, item in ipairs(items) do
    local label = string.format("[%d] %s", i, item.seq)
    local buf = create_sim_buffer(lines, label)

    ---@type integer
    local win
    if i == 1 then
      win = v.nvim_get_current_win()
    elseif i == ROW_CAPACITY + 1 then
      cmd("botright split")
      win = v.nvim_get_current_win()
    else
      cmd("vsplit")
      win = v.nvim_get_current_win()
    end

    setup_sim_window(win, buf, lines, row, col, label)
    ---@type VimficiencyReplayWin
    local replay_win = { win = win, buf = buf, seq_idx = i }
    table.insert(multi_sim.windows, replay_win)
  end

  if v.nvim_buf_is_valid(tabnew_buf) then
    v.nvim_buf_delete(tabnew_buf, { force = true })
  end

  cmd("wincmd =")

  if #multi_sim.windows > 0 and v.nvim_win_is_valid(multi_sim.windows[1].win) then
    v.nvim_set_current_win(multi_sim.windows[1].win)
  end

  local primary = v.nvim_get_current_win()
  create_status_bar()
  if v.nvim_win_is_valid(primary) then
    v.nvim_set_current_win(primary)
  end

  -- Render the initial snapshot in each window.
  for _, entry in ipairs(multi_sim.windows) do
    local states = multi_sim.states[entry.seq_idx]
    if states and #states > 0 then
      apply_state(entry, states[1])
    end
  end
  refresh()
end

-- =============================================================================
-- Focus / escape (command handlers)
-- =============================================================================

--- Focus one replay buffer without tearing down the others.
---@diagnostic disable-next-line: redefined-local
user_focus = function(idx)
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
    v.nvim_set_option_value("bufhidden", "hide", { buf = entry.buf })
  end

  local focus_win = multi_sim.windows[idx].win
  v.nvim_set_current_win(focus_win)
  cmd("only")

  multi_sim.windows = { { win = focus_win, buf = saved_bufs[idx], seq_idx = idx } }
  focus_state = { saved_bufs = saved_bufs, focused_idx = idx }
  refresh()
end

--- Restore the split replay layout.
user_escape = function()
  if not focus_state then
    vim.notify("vimficiency: not currently focused", vim.log.levels.INFO)
    return
  end
  local saved_bufs = focus_state.saved_bufs
  local focused_idx = focus_state.focused_idx

  local current_win = v.nvim_get_current_win()
  v.nvim_win_set_buf(current_win, saved_bufs[1])
  decorate_sim_window(current_win)

  ---@type VimficiencyReplayWin[]
  local new_windows = { { win = current_win, buf = saved_bufs[1], seq_idx = 1 } }
  local ROW_CAPACITY = 4
  for i = 2, #saved_bufs do
    if i == ROW_CAPACITY + 1 then
      cmd("botright split")
    else
      cmd("vsplit")
    end
    local new_win = v.nvim_get_current_win()
    v.nvim_win_set_buf(new_win, saved_bufs[i])
    decorate_sim_window(new_win)
    new_windows[i] = { win = new_win, buf = saved_bufs[i], seq_idx = i }
  end

  -- Restore normal replay teardown behavior.
  for _, buf in ipairs(saved_bufs) do
    if v.nvim_buf_is_valid(buf) then
      v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
    end
  end

  cmd("wincmd =")
  v.nvim_set_current_win(new_windows[focused_idx].win)

  multi_sim.windows = new_windows
  focus_state = nil
  -- Non-focused buffers were frozen while focused.
  seek_to(multi_sim.global_step)
end

-- =============================================================================
-- Public API
-- =============================================================================

--- Simulate multiple sequences side-by-side in a new tab.
--- Replay snapshots are precomputed through the hidden Neovim probe window.
---@class VimficiencyReplayOpts
---@field label   string?   Display label shown in the replay statusline.
---@field end_row integer?  0-indexed end row of the captured session.
---@field end_col integer?  0-indexed end col.

---@param lines string[] Buffer content to simulate on
---@param row integer 0-indexed starting row
---@param col integer 0-indexed starting column
---@param items VimficiencyReplayItem[] Each item is `{ seq, cost? }`.
---@param opts VimficiencyReplayOpts? Optional display extras.
function M.simulate_compare(lines, row, col, items, opts)
  if #multi_sim.windows > 0 then
    cleanup_multi_sim()
  end
  if not items or #items == 0 then
    vim.notify("simulate_compare: no sequences provided", vim.log.levels.ERROR)
    return
  end
  if not lines or #lines == 0 then
    vim.notify("simulate_compare: no lines provided", vim.log.levels.ERROR)
    return
  end

  opts = opts or {}
  multi_sim.global_step = 0
  multi_sim.replay_label = opts.label
  multi_sim.start_row = row
  multi_sim.start_col = col
  multi_sim.end_row = opts.end_row
  multi_sim.end_col = opts.end_col
  multi_sim.saved_win = v.nvim_get_current_win()
  multi_sim.saved_tab = v.nvim_get_current_tabpage()
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.costs = {}
  multi_sim.states = {}
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  local my_gen = multi_sim.precompute_gen

  ---@type VimficiencyToken[][]
  local tokenized = {}
  for i, item in ipairs(items) do
    tokenized[i] = tokenize_for_animation(item.seq)
    multi_sim.sequences[i] = tokenized[i]
    multi_sim.costs[i] = item.cost
  end

  vim.notify("vimficiency: precomputing replay…", vim.log.levels.INFO)

  local function precompute_next(idx)
    if multi_sim.precompute_gen ~= my_gen then
      return
    end
    if idx > #items then
      build_sim_ui(lines, row, col, items)
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
M._debug_get_focus_state = function() return focus_state end
M._debug_seek_to = seek_to
M._debug_toggle_focus = user_toggle_focus
M._debug_cycle_next = user_cycle_next
M._debug_cycle_prev = user_cycle_prev
M._debug_tokenize_for_animation = tokenize_for_animation

return M
