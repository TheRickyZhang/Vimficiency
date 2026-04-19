-- lua/vimficiency/simulate.lua
local v       = vim.api
local cmd     = vim.cmd
local max     = math.max
local min     = math.min
local ffi_lib = require("vimficiency.ffi")
local util    = require("vimficiency.util")

local M = {}

-- Namespace for cursor highlight extmarks
local cursor_ns = v.nvim_create_namespace("vimficiency_cursor")

-- Highlight groups. `default = true` lets users override without us clobbering
-- their config.
v.nvim_set_hl(0, "VimficiencyReplayCurrent",      { link = "IncSearch", default = true })
-- Normal-mode replay cursor — distinct from `Cursor` so the simulated cursor
-- is visually different from the user's own cursor (which uses `Cursor`).
-- `Search` is typically a bright block, unused elsewhere in replay UI.
v.nvim_set_hl(0, "VimficiencyReplayCursor",       { link = "Search",    default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursorInsert", { link = "DiffAdd",   default = true })
v.nvim_set_hl(0, "VimficiencyReplayCursorVisual", { link = "Visual",    default = true })
-- Label color for the currently-focused sim window's `[N]` marker. Linked to
-- `Title` by default so it stands out from surrounding `Comment`-grey labels
-- without introducing a new color into the user's colorscheme.
v.nvim_set_hl(0, "VimficiencyReplayActive",       { link = "Title",     default = true })

-- =============================================================================
-- Multi-sequence replay state
-- =============================================================================

---@class VimficiencyReplayWin
---@field win integer
---@field buf integer
---@field seq_idx integer   -- Index into `multi_sim.sequences` / `.states` /
---                          -- `.costs` for this window's sequence. In the
---                          -- split layout this equals the position in
---                          -- `multi_sim.windows`, but in focus mode (and
---                          -- after `<Tab>`/`<S-Tab>` cycling) the two diverge — use
---                          -- this field, never the iteration index.

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
---@field sequences string[][]
---@field costs (string?)[]            -- parallel to sequences; nil = no cost row
---@field states ReplaySnapshot[][]    -- states[window_i][step_idx+1]; states[_][1] = step 0
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

---@type table<string, boolean>
local VISUAL_ENTER_COMMANDS = {
  ["v"] = true,
  ["V"] = true,
  ["<C-v>"] = true,
  ["gh"] = true,
  ["gH"] = true,
}

--- Whether a token enters a modal state that should be sampled asynchronously.
--- For plain Normal-mode motions/operators we can force-drain with `feedkeys(x)`
--- and then sample, but commands that enter Insert/Visual need the old
--- yield-based path so replay can observe the intermediate mode directly.
---@param token string
---@return boolean
local function enters_modal_state(token)
  local bare = token:gsub("^%d+", "")
  return is_change_command(token) or VISUAL_ENTER_COMMANDS[bare] == true
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

--- Render multiline virtual header lines above the first buffer line.
--- `seq_idx` is the logical sequence index (from `entry.seq_idx`), not the
--- position in `multi_sim.windows` — those differ in focus mode and after
--- `<Tab>`/`<S-Tab>` cycling.
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

  -- Use a distinct highlight for the `[N]` label when this window has focus.
  -- Non-intrusive (label only, no flood fill) but noticeable enough that a
  -- user scanning the replay tab can tell at a glance which buffer their
  -- cursor is in.
  local is_active = entry.win == v.nvim_get_current_win()
  local label_hl = is_active and "VimficiencyReplayActive" or "Comment"

  ---@type table[]
  local virt_lines = {
    -- Leading blank row for symmetry with the trailing padding below —
    -- gives the header breathing room on both sides of the info row.
    { { "", "Normal" } },
    -- Row 1: `[N]` label + local step counter. The focused window's `[N]`
    -- picks up `VimficiencyReplayActive` via `label_hl`, so this row is
    -- also the "which buffer has my cursor" indicator.
    {
      { string.format("[%d] ", seq_idx), label_hl },
      { string.format("Local %d/%d", local_step, #tokens), "Comment" },
    },
    -- Row 2: Mode on its own line — label is plain (not `mode_hl`) since
    -- the buffer cursor is already mode-colored.
    {
      { "Mode ", "Comment" },
      { mode_label(snap.mode), "Normal" },
    },
  }
  if multi_sim.costs[seq_idx] then
    virt_lines[#virt_lines + 1] = {
      { "Cost ", "Comment" },
      -- Plain Normal (not Title/blue) — blue is reserved for the focused
      -- `[N]` label marker, and reusing it here would muddle "which buffer
      -- has my cursor" with "here's a cost value".
      { multi_sim.costs[seq_idx], "Normal" },
    }
  end
  vim.list_extend(virt_lines, wrapped_sequence)
  -- Blank trailing virt_line for breathing room between the header and
  -- the buffer content below.
  virt_lines[#virt_lines + 1] = { { "", "Normal" } }

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

-- Forward-declared so `cleanup_multi_sim` (defined below, called from
-- `q` and from `:Vimfy reload`'s shutdown path) can reference the
-- status-bar teardown. The concrete assignment lives further down
-- alongside the `create_status_bar` / `update_status_bar` trio.
---@type fun()
local destroy_status_bar

--- Tear down the replay tab, clear state, return focus to the saved window.
local function cleanup_multi_sim()
  -- Bump the generation so any in-flight precompute callback short-circuits
  -- before touching torn-down state.
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1

  destroy_status_bar()

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

--- Build the per-window statusline text for a replay buffer. Shows global
--- info that doesn't need to repeat per-buffer (session label, global
--- step, start → end cursor positions). Positions render as `row:col`
--- and are 1-indexed for display. The per-buffer virt-lines header still
--- carries local info.
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
  -- Escape `%` so no chunk is interpreted as a statusline directive —
  -- matters if the session label itself contains one.
  return (text:gsub("%%", "%%%%"))
end

--- Create a 1-line horizontal split at the bottom of the sim tab for the
--- global replay info (session label, step, start → end positions). A
--- real window beats overriding `vim.o.statusline` because statusline
--- plugins (lualine, heirline, etc.) re-set `vim.o.statusline` on many
--- events and would stomp any `%!` expression we install. The split
--- also sits exactly where "above the global statusline" means visually.
---
--- Styling: `winhighlight=Normal:StatusLine` makes the bar look like a
--- native statusline band; `statusline=" "` + `StatusLineNC:Normal`
--- hides the border separator above it so it reads as one unbroken line.
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

-- Assigned to the forward-declared `destroy_status_bar` above so
-- `cleanup_multi_sim` resolves it as the same local upvalue.
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

--- Full visual refresh: cursor highlights, virtual headers, statuslines,
--- screen redraw.
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
  for _, entry in ipairs(multi_sim.windows) do
    local states = multi_sim.states[entry.seq_idx]
    if states and #states > 0 then
      apply_state(entry, states[min(target + 1, #states)])
    end
  end
  refresh()
end

--- Upper bound on `global_step` for forward stepping. In split mode this
--- is the longest sequence across all buffers. In focus mode it's the
--- focused sequence's own length, so the user can't advance past the
--- end of what they're currently looking at — once the displayed buffer
--- is saturated, `<Right>` becomes a no-op instead of silently bumping
--- `global_step` against hidden siblings.
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

--- `<Left>`: step backward via snapshot lookup.
local function user_step_left()
  if multi_sim.global_step > 0 then
    seek_to(multi_sim.global_step - 1)
  else
    refresh()
  end
end

-- Forward-declared so `user_toggle_focus` can reference them; the actual
-- assignments live in the "Focus / escape" section below. The command
-- subcommands and `M.focus` / `M.escape` exports read these same locals,
-- so users of either entry point go through the same code path.
---@type fun(idx: integer)
local user_focus
---@type fun()
local user_escape

--- `<Tab>` / `<S-Tab>`: "next/prev sim sequence", behaviour depends on mode:
---   * split layout → move the cursor to the next/previous sim window (wraps
---     around; feels like `:bnext`/`:bprevious` but confined to the sim tab).
---   * focus mode   → swap the currently-displayed buffer for the next/previous
---     sim sequence in place, without leaving focus.
--- Wrapping is deliberate; two buffers are enough for the wrap to feel natural.
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
        -- The WinEnter autocmd on the entered sim buffer refreshes the
        -- focus-marker highlight; no explicit refresh needed here.
      end
      return
    end
  end
end

local function user_cycle_next() user_cycle(1) end
local function user_cycle_prev() user_cycle(-1) end

--- `<leader>y`: copy the current window's sequence string into the unnamed
--- and `+` (clipboard) registers. Derives the string by concatenating the
--- tokenized form, which round-trips the original — the tokenizer preserves
--- token boundaries but not extra formatting, and we don't need more than
--- "paste this into a new buffer to rerun it".
local function user_yank_sequence()
  local cur_win = v.nvim_get_current_win()
  for _, entry in ipairs(multi_sim.windows) do
    if entry.win == cur_win then
      local tokens = multi_sim.sequences[entry.seq_idx]
      if not tokens then return end
      local seq = table.concat(tokens, "")
      vim.fn.setreg('"', seq)
      vim.fn.setreg('+', seq)
      vim.notify("vimficiency: yanked [" .. entry.seq_idx .. "] " .. seq,
        vim.log.levels.INFO)
      return
    end
  end
end

--- Dump replay diagnostic state to `:messages`. Intended as a debug hook the
--- user can fire from inside a replay buffer (`D`) when something looks off
--- — captures global_step, each window's seq_idx/precomputed snapshot, the
--- live rendered cursor, the active token, and focus_state. Output is
--- intentionally plain-text so it copies cleanly into bug reports.
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
    pr(string.format("  active token = %s  (global_step = %d)",
      active_token and string.format("%q", active_token) or "nil",
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
      -- Per-token precompute trace: reveals whether the feedkeys took
      -- effect before the post-yield snap was taken. If the cursor /
      -- mode at `after_feedkeys` and `after_yield_2` disagree with the
      -- final snapshot position in a way the token doesn't explain,
      -- that's a precompute-oracle race in the making.
      if snap.trace then
        pr(string.format("  trace for token %s:",
          snap.token and string.format("%q", snap.token) or "<initial>"))
        local prev_t = nil
        for _, p in ipairs(snap.trace) do
          local dt = prev_t and string.format("  +%.2fms",
            (p.t_ns - prev_t) / 1e6) or ""
          prev_t = p.t_ns
          pr(string.format(
            "    %-18s cursor=(%d,%d) mode=%s current_win=%d%s",
            p.label, p.cursor[1], p.cursor[2], p.mode, p.current_win, dt))
        end
      end
    else
      pr(string.format("  snapshot[%d]          = MISSING", snap_idx))
    end
    pr(string.format("    live    line[%d] = %s",
      rendered[1], vim.inspect(live_lines[rendered[1]] or "")))
    pr(string.format("  tokens = %s", vim.inspect(tokens)))
  end
  pr("=== end ===")

  local msg = table.concat(out, "\n")
  -- Also echo a short toast so the user knows the dump landed.
  vim.notify("vimficiency: debug dump written to :messages", vim.log.levels.INFO)
  v.nvim_echo({ { msg, "Normal" } }, true, {})
end

--- Focus the sim window under the cursor, or escape back to split layout if
--- already focused. Buffer-local keymap target for `<CR>` — a one-key way to
--- zoom into whichever buffer the user is currently looking at without
--- retyping the `:Vimfy focus <N>` command.
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
---@field group string?     -- short label for the startup echo; entries sharing
---                          a group are merged (e.g. "<Left>/<Right> step").
---                          nil means bound but not advertised.

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

--- Attach every entry in `REPLAY_KEYMAPS` to a sim buffer. `nowait = true`
--- avoids the leader-timeout disambiguation that affects `q` (Vim's macro
--- recording prefix) and is harmless consistency for the other keys.
---@param buf integer
local function attach_replay_keymaps(buf)
  util.set_buffer_keymaps(buf, REPLAY_KEYMAPS)
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
--- Flag rationale: `nvim_feedkeys(..., "n", false)` is the default because
--- replay needs to observe intermediate modal states (Insert / Visual) rather
--- than flatten them the way `:normal`-style execution can. For tokens that
--- start in Normal mode and do not enter a modal state we can safely add `x`
--- to synchronously drain typeahead before sampling. That closes the first-
--- token race seen on plain motions like `j` / `$` without disturbing the
--- mode oracle for `i`, `s`, `v`, etc. We still yield twice after every feed:
--- even once a key has executed, `nvim_get_mode()` has historically lagged one
--- tick behind some transitions (notably `i` → `<Esc>`).
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

  -- Non-intrusive per-token telemetry sampled at four points around each
  -- `feed_and_yield`. Attached to the post-token snapshot so the `D`
  -- debug dump can surface it live — lets us diagnose
  -- precompute-oracle races (first-token dropouts, mode-lag) without
  -- having to reproduce them synthetically.
  ---@param label string
  ---@return table
  local function probe_debug(label)
    local cur = { -1, -1 }
    if v.nvim_win_is_valid(probe_win) then
      local ok, c = pcall(v.nvim_win_get_cursor, probe_win)
      if ok then cur = c end
    end
    return {
      label       = label,
      cursor      = cur,
      mode        = v.nvim_get_mode().mode,
      current_win = v.nvim_get_current_win(),
      t_ns        = vim.uv.hrtime(),
    }
  end

  local co = coroutine.create(function()
    -- Yield helper: feed keys, then yield *twice*. Empirically, a single
    -- defer tick is enough for the input queue to drain, but `nvim_get_mode()`
    -- sometimes lags one more tick behind a mode transition (observed reliably
    -- for `i` → `<Esc>` with only one intermediate feed). Two yields makes the
    -- mode query stable across all mode transitions we care about. Cost: 2×
    -- event-loop ticks per token — still imperceptible for typical replays.
    ---@param keys string
    ---@param token string
    ---@return table[]|nil trace  four-point trace, or nil if probe died
    local function feed_and_yield(keys, token)
      if not v.nvim_win_is_valid(probe_win) then return nil end
      local trace = { probe_debug("before_feed") }
      v.nvim_set_current_win(probe_win)
      local mode_flags = "n"
      local curr_mode = v.nvim_get_mode().mode
      if curr_mode:sub(1, 1) == "n" and not enters_modal_state(token) then
        mode_flags = "nx"
      end
      v.nvim_feedkeys(keys, mode_flags, false)
      trace[#trace + 1] = probe_debug("after_feedkeys")
      coroutine.yield()
      trace[#trace + 1] = probe_debug("after_yield_1")
      coroutine.yield()
      trace[#trace + 1] = probe_debug("after_yield_2")
      return trace
    end

    -- No prelude <Esc>: opening the probe float via `nvim_open_win(_, true, ...)`
    -- makes it the current window, and a window switch naturally exits insert
    -- mode. Pre-feeding `<Esc>` in the already-normal state was empirically
    -- introducing an off-by-one in the first post-feed mode snapshot.
    local initial = snap()
    initial.trace = { probe_debug("initial") }
    table.insert(states, initial)

    for _, token in ipairs(tokens) do
      -- If the probe window died underneath us (rare but possible if an
      -- autocmd or concurrent simulate_compare bulldozed it), bail cleanly.
      if not v.nvim_win_is_valid(probe_win) then return end
      local trace = feed_and_yield(v.nvim_replace_termcodes(token, true, false, true), token)
      local s = snap()
      s.trace = trace
      s.token = token
      table.insert(states, s)
    end

    -- Flush residual mode so nothing bleeds into the user's session when the
    -- sequence ended mid-insert or mid-visual (no trailing `<Esc>` in tokens).
    feed_and_yield(esc, "<Esc>")
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

  -- Re-render headers when the user shifts focus between sim windows
  -- (e.g. via `<C-w>l`), so the `[N]` label on the newly-current window
  -- picks up the `VimficiencyReplayActive` highlight. Scoped per-buffer so
  -- the autocmd vanishes when the buffer is wiped on replay teardown.
  v.nvim_create_autocmd("WinEnter", {
    buffer = buf,
    callback = function() update_cursor_highlights() end,
    desc = "vimficiency: refresh focus indicator on window enter",
  })

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
---@param items VimficiencyReplayItem[]
local function build_sim_ui(lines, row, col, items)
  cmd("tabnew")
  multi_sim.sim_tab = v.nvim_get_current_tabpage()
  local tabnew_buf = v.nvim_get_current_buf()

  for i, item in ipairs(items) do
    -- `multi_sim.sequences[i]` / `multi_sim.costs[i]` were set eagerly in
    -- `simulate_compare`, so no assignment needed here.
    local label = string.format("[%d] %s", i, item.seq)
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
    local replay_win = { win = win, buf = buf, seq_idx = i }
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

  -- Create the replay statusline band at the bottom of the tab, then
  -- return focus to the first sim window so stepping keymaps fire in
  -- the right place on first keypress.
  local primary = v.nvim_get_current_win()
  create_status_bar()
  if v.nvim_win_is_valid(primary) then
    v.nvim_set_current_win(primary)
  end

  -- Render the initial step-0 state in each window, then refresh highlights/winbars.
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

--- `:Vimfy focus <N>` / buffer-local `<CR>`: collapse to a single window
--- showing the Nth sim buffer full-screen. Stepping still works; other
--- buffers stay alive (bufhidden = "hide") so `:Vimfy escape` or `<CR>`
--- (from within focus) can restore the side-by-side layout.
---
--- Assigns to the forward-declared `user_focus` so `user_toggle_focus`
--- (which is referenced by the `<CR>` keymap before this file reaches
--- here) routes through the same code path.
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
    -- Prevent `:only` from wiping the non-focused buffers.
    v.nvim_set_option_value("bufhidden", "hide", { buf = entry.buf })
  end

  local focus_win = multi_sim.windows[idx].win
  v.nvim_set_current_win(focus_win)
  cmd("only")

  multi_sim.windows = { { win = focus_win, buf = saved_bufs[idx], seq_idx = idx } }
  focus_state = { saved_bufs = saved_bufs, focused_idx = idx }
  refresh()
end

--- `:Vimfy escape` / buffer-local `<CR>` (from within focus): restore the
--- side-by-side replay layout and re-sync every buffer to the current
--- global_step (non-focused buffers were frozen during focus mode).
--- Assigned to the forward-declared `user_escape` for the same reason as
--- `user_focus` above.
user_escape = function()
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
  local new_windows = { { win = current_win, buf = saved_bufs[1], seq_idx = 1 } }
  for i = 2, #saved_bufs do
    cmd("vsplit")
    local new_win = v.nvim_get_current_win()
    v.nvim_win_set_buf(new_win, saved_bufs[i])
    decorate_sim_window(new_win)
    new_windows[i] = { win = new_win, buf = saved_bufs[i], seq_idx = i }
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
---@class VimficiencyReplayOpts
---@field label   string?   Display label shown in the replay statusline
---                         (typically the session alias). Nil falls back
---                         to just "vimficiency".
---@field end_row integer?  0-indexed end row of the captured session.
---@field end_col integer?  0-indexed end col. Rendered as `row:col → row:col`
---                         in the statusline when both end_row and end_col
---                         are set; hidden otherwise.

---@param lines string[] Buffer content to simulate on
---@param row integer 0-indexed starting row
---@param col integer 0-indexed starting column
---@param items VimficiencyReplayItem[] Each `{ seq, cost? }`; cost is an
---   optional pre-formatted string rendered in the per-buffer header.
---@param opts VimficiencyReplayOpts? Optional display extras (session
---   label, end position). Nil treats everything as absent.
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

  ---@type string[][]
  local tokenized = {}
  for i, item in ipairs(items) do
    tokenized[i] = tokenize_for_animation(item.seq)
    -- Populate sequences/costs eagerly so things that consult them
    -- (header rendering, tests probing the tokenization) work before the
    -- async precompute has built the sim UI.
    multi_sim.sequences[i] = tokenized[i]
    multi_sim.costs[i] = item.cost
  end

  vim.notify("vimficiency: precomputing replay…", vim.log.levels.INFO)

  local function precompute_next(idx)
    if multi_sim.precompute_gen ~= my_gen then
      -- A newer simulate_compare superseded us; abandon quietly.
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
