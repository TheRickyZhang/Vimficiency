-- lua/vimficiency/simulate.lua
local v       = vim.api
local cmd     = vim.cmd
local max     = math.max
local min     = math.min
local ffi_lib = require("vimficiency.ffi")
local sequence_display = require("vimficiency.sequence_display")
local util    = require("vimficiency.util")
local highlights = require("vimficiency.highlights")

local M = {}

local cursor_ns = v.nvim_create_namespace("vimfy_cursor")

-- =============================================================================
-- Multi-sequence replay state
-- =============================================================================

---@class VF.Replay.Window
---@field win integer
---@field buf integer
---@field is_user boolean              -- true for the leftmost user-sequence pane
---@field seq_idx integer              -- suggestion rank (1..#suggestions) when is_user=false; 0 for user pane
---@field default_rank integer         -- original suggestion rank assigned at build_sim_ui time
                                       --   (used only for the index indicator; unused for user panes)

---@class VF.Replay.Snapshot
---@field lines string[]
---@field cursor [integer, integer]   -- 1-indexed row, 0-indexed col (as nvim returns)
---@field mode string                  -- nvim_get_mode().mode code ("n", "i", "v", "V", "\22", etc.)

---@class VF.Replay.PoolEntry
---@field tokens VF.Sequence.Token[]
---@field cost string?                 -- pre-formatted cost string; nil = no cost row
---@field states VF.Replay.Snapshot[]?     -- nil until precompute completes for this entry

---@class VF.Replay.Pool
---@field user VF.Replay.PoolEntry?        -- the user's typed sequence (if any); displayed by is_user panes
---@field suggestions VF.Replay.PoolEntry[]  -- rank-indexed (1..N) optimal_results

---@class VF.Replay.State
---@field global_step integer
---@field windows VF.Replay.Window[]
---@field pool VF.Replay.Pool
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
---@field source_lines string[]?
---@field source_row integer?
---@field source_col integer?
---@field source_opts table?

---@type VF.Replay.State
local multi_sim = {
  global_step = 0,
  windows = {},
  pool = { user = nil, suggestions = {} },
  saved_win = nil,
  saved_tab = nil,
  sim_tab = nil,
  replay_label = nil,
  precompute_gen = 0,
  source_lines = nil,
  source_row = nil,
  source_col = nil,
  source_opts = nil,
}

--- Look up the pool entry a window is currently displaying. Returns nil
--- if the entry is missing (e.g. user pane but pool.user is nil).
---@param entry VF.Replay.Window
---@return VF.Replay.PoolEntry?
local function pool_entry_for(entry)
  if entry.is_user then return multi_sim.pool.user end
  return multi_sim.pool.suggestions[entry.seq_idx]
end

---@class VF.Replay.FocusSavedEntry
---@field buf integer
---@field is_user boolean
---@field seq_idx integer
---@field default_rank integer

---@class VF.Replay.FocusState
---@field saved_entries VF.Replay.FocusSavedEntry[]
---@field focused_idx integer

---@type VF.Replay.FocusState?
local focus_state = nil

-- =============================================================================
-- Tokenization (sequence string → animation steps)
-- =============================================================================
--
-- Tokens carry a `kind` tagged by the C++ parser — see `VF.Sequence.Token` in
-- `ffi.lua`. The historical Lua-side classifier tables (INSERT_COMMANDS,
-- VISUAL_ENTER_COMMANDS, NEEDS_FOLLOWING_KEY) are gone: they were a parallel
-- source of truth to the parser's grammar and drifted silently. See
-- `dev/lua/replay-precompute.md` for the gating semantics.

--- Whether a token itself starts a modal state.
---@param token VF.Sequence.Token
---@return boolean
local function enters_modal_state(token)
  return token.kind == "change" or token.kind == "visual"
end

-- Fallback-only classifier for the char-by-char split path, which runs
-- when both C++ tokenizers fail (e.g., truly malformed sequences the
-- grammar can't parse). Much smaller than the old classifier set because
-- C++ now handles visual-mode entry; this covers the residual cases and
-- defaults non-matches to "movement" (fast-path-eligible, safe).
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
---@return "movement"|"change"|"visual"
local function classify_fallback(text)
  local bare = text:gsub("^%d+", "")
  if FALLBACK_INSERT_BARE[bare] then return "change" end
  if FALLBACK_VISUAL_BARE[bare] then return "visual" end
  -- `c{motion}` / `c{textobj}` — any `c`-prefixed token of length >1 that
  -- isn't in the bare insert table. (`cc` matched above.)
  if bare:sub(1, 1) == "c" and #bare > 1 then return "change" end
  return "movement"
end

--- Tokenize a sequence for animation. Returns kinded tokens — consumers
--- (the precompute coroutine, header rendering, debug dump) should use
--- `.text` when feeding nvim and `.kind` when gating modal behavior.
---@param seq string
---@return VF.Sequence.Token[] tokens
local function tokenize_for_animation(seq)
  local tokens, err = ffi_lib.tokenize_sequence(seq)
  if err or not tokens or #tokens == 0 then
    tokens, err = ffi_lib.tokenize_movements(seq)
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
      ---@type VF.Sequence.Token[]
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

  local CHUNK_SIZE = 4

  local function typed_chunks(text)
    local chunks = {}
    local chunk = ""
    local chunk_units = 0
    local function flush_chunk()
      if chunk == "" then return end
      chunks[#chunks + 1] = chunk
      chunk = ""
      chunk_units = 0
    end
    local i = 1
    while i <= #text do
      if text:sub(i, i) == "<" then
        local close = text:find(">", i, true)
        if close then
          flush_chunk()
          chunks[#chunks + 1] = text:sub(i, close)
          i = close + 1
        else
          chunk = chunk .. text:sub(i, i)
          chunk_units = chunk_units + 1
          i = i + 1
        end
      else
        chunk = chunk .. text:sub(i, i)
        chunk_units = chunk_units + 1
        i = i + 1
      end

      if chunk_units >= CHUNK_SIZE then
        flush_chunk()
      end
    end
    flush_chunk()
    return chunks
  end

  -- Happy path: tokens already carry `.kind` from the C++ parser. Chunk
  -- any `typed` tokens into small pieces so the animation shows typed
  -- text materializing gradually, not as one jump. Keep `<...>` forms
  -- intact because captured insert-mode spaces/CRs arrive as feedable
  -- key notation from `keytrans`.
  ---@type VF.Sequence.Token[]
  local expanded = {}
  for _, tok in ipairs(tokens) do
    if tok.kind == "typed" then
      for _, chunk in ipairs(typed_chunks(tok.text)) do
        expanded[#expanded + 1] = {
          text = chunk,
          kind = "typed",
        }
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
  if mode:sub(1, 1) == "i" then return highlights.REPLAY_CURSOR_INSERT end
  if mode == "v" or mode == "V" or mode == "\22" then
    return highlights.REPLAY_CURSOR_VISUAL
  end
  return highlights.REPLAY_CURSOR
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

--- Current snapshot for a window, clamped to the end of its sequence.
---@param entry VF.Replay.Window
---@return VF.Replay.Snapshot?
local function current_snap(entry)
  local pool_entry = pool_entry_for(entry)
  if not pool_entry or not pool_entry.states or #pool_entry.states == 0 then
    return nil
  end
  local states = assert(pool_entry.states)
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

--- Label shown as the pane's header tag.
---@param entry VF.Replay.Window
---@return string
local function header_label(entry)
  if entry.is_user then return "[you] " end
  return string.format("[%d] ", entry.seq_idx)
end

--- Build the virtual header above a replay buffer.
---@param entry VF.Replay.Window
---@return table[]?
local function build_virt_lines(entry)
  local snap = current_snap(entry)
  if not snap then return nil end

  local pool_entry = pool_entry_for(entry)
  if not pool_entry then return nil end

  local tokens = pool_entry.tokens
  local local_step = min(multi_sim.global_step, #tokens)
  local width = max(8, v.nvim_win_get_width(entry.win))
  local raw_parts = {}
  for _, tok in ipairs(tokens) do
    raw_parts[#raw_parts + 1] = tok.text
  end
  local display_lines = sequence_display.lines(table.concat(raw_parts, ""))

  local is_active = entry.win == v.nvim_get_current_win()
  local label_hl = is_active and highlights.REPLAY_ACTIVE or "Comment"

  ---@type table[]
  local info_row = {
    { header_label(entry), label_hl },
    { string.format("Local %d/%d", local_step, #tokens), "Comment" },
  }
  -- Rank indicator: only on the focused, non-user pane, and only when
  -- it has been cycled away from its default suggestion rank. Hidden
  -- otherwise so the display stays quiet at rest.
  if is_active and not entry.is_user
     and entry.default_rank and entry.seq_idx ~= entry.default_rank then
    info_row[#info_row + 1] = {
      string.format("  rank %d/%d", entry.seq_idx, #multi_sim.pool.suggestions),
      highlights.REPLAY_ACTIVE,
    }
  end

  ---@type table[]
  local virt_lines = {
    { { "", "Normal" } },
    info_row,
    {
      { "Mode ", "Comment" },
      { mode_label(snap.mode), "Normal" },
    },
  }
  if pool_entry.cost then
    virt_lines[#virt_lines + 1] = {
      { "Cost ", "Comment" },
      { pool_entry.cost, "Normal" },
    }
  end
  local seq_label = "Sequence "
  local seq_indent = string.rep(" ", #seq_label)
  local emitted_first = false
  for i, line in ipairs(display_lines) do
    local wrapped = wrap_chunks({ { line, "Normal" } },
      max(1, width - #seq_label))
    for _, chunks in ipairs(wrapped) do
      local prefix = emitted_first
        and { seq_indent, "Normal" }
        or { seq_label, "Comment" }
      virt_lines[#virt_lines + 1] = vim.list_extend({ prefix }, chunks)
      emitted_first = true
    end
  end
  virt_lines[#virt_lines + 1] = { { "", "Normal" } }

  return virt_lines
end

---@param entry VF.Replay.Window
---@param virt_lines table[]?
---@param target_height integer
local function apply_virt_lines(entry, virt_lines, target_height)
  if not virt_lines then return end

  while #virt_lines < target_height - 1 do
    virt_lines[#virt_lines + 1] = { { "", "Normal" } }
  end

  local width = max(8, v.nvim_win_get_width(entry.win))
  virt_lines[#virt_lines + 1] = { { string.rep("─", width), "Comment" } }

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
  local cached = {}
  local max_content = 0
  for i, entry in ipairs(multi_sim.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      cached[i] = build_virt_lines(entry)
      if cached[i] then
        max_content = max(max_content, #cached[i])
      end
    end
  end
  local target_height = max_content + 1

  for i, entry in ipairs(multi_sim.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_clear_namespace(entry.buf, cursor_ns, 0, -1)
      apply_virt_lines(entry, cached[i], target_height)

      local snap = current_snap(entry)
      local hl   = snap and mode_hl(snap.mode) or highlights.REPLAY_CURSOR

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
    for _, saved in ipairs(focus_state.saved_entries) do
      if v.nvim_buf_is_valid(saved.buf) then
        v.nvim_buf_delete(saved.buf, { force = true })
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
  multi_sim.pool = { user = nil, suggestions = {} }
  multi_sim.saved_win = nil
  multi_sim.saved_tab = nil
  multi_sim.sim_tab = nil
  multi_sim.replay_label = nil
  multi_sim.start_row = nil
  multi_sim.start_col = nil
  multi_sim.end_row = nil
  multi_sim.end_col = nil
  multi_sim.source_lines = nil
  multi_sim.source_row = nil
  multi_sim.source_col = nil
  multi_sim.source_opts = nil
  focus_state = nil
end

-- =============================================================================
-- Step advancement & winbar rendering
-- =============================================================================

--- Longest sequence length across every pane currently displayed.
---@return integer
max_total_steps = function()
  local m = 0
  for _, entry in ipairs(multi_sim.windows) do
    local pool_entry = pool_entry_for(entry)
    if pool_entry and #pool_entry.tokens > m then
      m = #pool_entry.tokens
    end
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
  local text = string.format(" vimfy%s  step %d/%d%s ",
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

  util.configure_scratch_window(win, {
    winfixheight = true,
    statusline   = " ",
    winhighlight = "Normal:StatusLine,StatusLine:Normal,StatusLineNC:Normal",
  })

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
---@param entry VF.Replay.Window
---@param snap  VF.Replay.Snapshot
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
    local pool_entry = pool_entry_for(entry)
    if pool_entry and pool_entry.states and #pool_entry.states > 0 then
      local states = assert(pool_entry.states)
      apply_state(entry, states[min(target + 1, #states)])
    end
  end
  refresh()
end

--- Upper bound for forward stepping in the current view.
---@return integer
local function step_cap()
  if focus_state then
    local entry = multi_sim.windows[1]
    if entry then
      local pool_entry = pool_entry_for(entry)
      if pool_entry then return #pool_entry.tokens end
    end
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
-- Forward-declared because the +/-/u/Up/Down key handlers need these
-- but the real definitions live further down with the pool helpers.
---@type fun(): VF.Replay.LayoutSlot[]
local build_layout_plan
---@type fun(ranks: VF.Replay.PoolRankKey[], cb: fun())
local ensure_states_for_ranks

--- Cycle to the next or previous replay pane.
--- In focus mode: swap the single visible buffer to the next/prev saved
--- pane (so <Tab>/<S-Tab> walks through the full set of panes that were
--- visible before focusing).
--- In split mode: move window focus to the next/prev pane.
---@param step integer   -- `+1` for next, `-1` for prev
local function user_cycle(step)
  if focus_state then
    local n = #focus_state.saved_entries
    if n < 2 then return end
    local new_idx = ((focus_state.focused_idx - 1 + step) % n + n) % n + 1
    local entry = multi_sim.windows[1]
    local saved = focus_state.saved_entries[new_idx]
    v.nvim_win_set_buf(entry.win, saved.buf)
    entry.buf          = saved.buf
    entry.is_user      = saved.is_user
    entry.seq_idx      = saved.seq_idx
    entry.default_rank = saved.default_rank
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
      local pool_entry = pool_entry_for(entry)
      if not pool_entry then return end
      ---@type string[]
      local parts = {}
      for _, tok in ipairs(pool_entry.tokens) do parts[#parts + 1] = tok.text end
      local seq = table.concat(parts, "")
      vim.fn.setreg('"', seq)
      vim.fn.setreg('+', seq)
      local tag = entry.is_user and "you" or tostring(entry.seq_idx)
      vim.notify("vimfy: yanked [" .. tag .. "] " .. seq,
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

  pr("=== vimfy replay debug ===")
  pr(string.format("global_step = %d (max = %d)",
    multi_sim.global_step, max_total_steps()))
  pr(string.format("focus_state = %s",
    focus_state and vim.inspect(focus_state) or "nil"))
  pr(string.format("current_win = %d", v.nvim_get_current_win()))

  for i, entry in ipairs(multi_sim.windows) do
    local pool_entry = pool_entry_for(entry) or { tokens = {}, states = {} }
    local states = pool_entry.states or {}
    local tokens = pool_entry.tokens or {}
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
      "window[%d]: win=%d buf=%d is_user=%s seq_idx=%d default_rank=%d #tokens=%d #states=%d",
      i, entry.win, entry.buf, tostring(entry.is_user), entry.seq_idx,
      entry.default_rank or 0, #tokens, #states))
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
  vim.notify("vimfy: debug dump written to :messages", vim.log.levels.INFO)
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
  vim.notify("vimfy: cursor is not in a replay window",
    vim.log.levels.WARN)
end

local function current_window_is_replay_window()
  local cur_win = v.nvim_get_current_win()
  for _, entry in ipairs(multi_sim.windows) do
    if entry.win == cur_win then
      return true
    end
  end
  return false
end

local function user_open_settings()
  if #multi_sim.windows == 0 or not current_window_is_replay_window() then
    vim.notify("vimfy: play settings are only available from a replay window",
      vim.log.levels.WARN)
    return
  end
  -- Rebuild the grid after the modal closes. Settings are persisted by
  -- the modal itself; relayout_grid reuses any cached precompute state
  -- so only brand-new ranks trigger fresh probes.
  require("vimficiency.play").open_settings({
    on_close = function() M.relayout_grid() end,
  })
end

--- `+` / `-`: adjust window_count in [1, 4] and relayout.
---@param step integer
local function user_adjust_window_count(step)
  local play = require("vimficiency.play")
  local settings = play.get_settings()
  local current = settings.window_count or 2
  local new_count = max(1, min(4, current + step))
  if new_count == current then return end
  play.set_setting("window_count", new_count)
  M.relayout_grid()
end

--- `u`: toggle whether the user pane is shown and relayout.
local function user_toggle_include_user()
  local play = require("vimficiency.play")
  local settings = play.get_settings()
  play.set_setting("include_user_sequence", not settings.include_user_sequence)
  M.relayout_grid()
end

--- `<Up>` / `<Down>`: cycle the suggestion displayed by the current pane.
--- No-op on user panes; no-op when only one suggestion exists. Up moves
--- to a better rank (smaller index); wraps. Changes are session-local
--- and never persisted.
---@param step integer   -- `-1` for Up (better rank), `+1` for Down
local function user_cycle_rank(step)
  if #multi_sim.windows == 0 then return end
  local cur_win = v.nvim_get_current_win()
  ---@type VF.Replay.Window?
  local entry
  for _, e in ipairs(multi_sim.windows) do
    if e.win == cur_win then entry = e; break end
  end
  if not entry or entry.is_user then return end

  local n = #multi_sim.pool.suggestions
  if n < 2 then return end
  local new_rank = ((entry.seq_idx - 1 + step) % n + n) % n + 1
  if new_rank == entry.seq_idx then return end
  entry.seq_idx = new_rank

  -- If the new rank's states aren't precomputed yet, we hit the lazy path.
  -- Render the header immediately (shows the new sequence + rank marker)
  -- and apply the new state when precompute finishes.
  update_cursor_highlights()
  update_status_bar()

  ensure_states_for_ranks({ new_rank }, function()
    local pool_entry = pool_entry_for(entry)
    if not pool_entry or not pool_entry.states then return end
    if not v.nvim_win_is_valid(entry.win) then return end
    local states = assert(pool_entry.states)
    apply_state(entry, states[min(multi_sim.global_step + 1, #states)])
    refresh()
  end)
end

-- =============================================================================
-- Buffer-local keymaps (declarative table + echo-hint derivation)
-- =============================================================================

---@class VF.Replay.Keymap
---@field lhs string
---@field handler fun()
---@field desc string       -- shown in `:map` listings
---@field summary_group string?  -- Adjacent matching groups collapse onto one help row.
---@field summary_desc string?   -- Optional help-row description for the collapsed group.

---@type VF.Replay.Keymap[]
local REPLAY_KEYMAPS = util.with_standard_ui_keymaps({
  { lhs = "<Left>",  handler = user_step_left,  desc = "Step backward",
    summary_group = "step", summary_desc = "Step backward / forward" },
  { lhs = "<Right>", handler = user_step_right, desc = "Step forward",
    summary_group = "step", summary_desc = "Step backward / forward" },
  { lhs = "<Tab>",   handler = user_cycle_next, desc = "Cycle pane focus (next)",
    summary_group = "cycle_pane", summary_desc = "Cycle pane focus (Tab / S-Tab)" },
  { lhs = "<S-Tab>", handler = user_cycle_prev, desc = "Cycle pane focus (prev)",
    summary_group = "cycle_pane", summary_desc = "Cycle pane focus (Tab / S-Tab)" },
  { lhs = "<Up>",    handler = function() user_cycle_rank(-1) end,
    desc = "Cycle this pane to a better-ranked suggestion",
    summary_group = "cycle_rank", summary_desc = "Cycle pane's suggestion rank (Up/Down)" },
  { lhs = "<Down>",  handler = function() user_cycle_rank(1) end,
    desc = "Cycle this pane to a worse-ranked suggestion",
    summary_group = "cycle_rank", summary_desc = "Cycle pane's suggestion rank (Up/Down)" },
  { lhs = "+",       handler = function() user_adjust_window_count(1) end,
    desc = "Add a pane (up to 4)",
    summary_group = "pane_count", summary_desc = "Add / remove a pane (+ / -)" },
  { lhs = "-",       handler = function() user_adjust_window_count(-1) end,
    desc = "Remove a pane (down to 1)",
    summary_group = "pane_count", summary_desc = "Add / remove a pane (+ / -)" },
  { lhs = "u",       handler = user_toggle_include_user,
    desc = "Toggle user-sequence pane" },
  { lhs = "<CR>",    handler = user_toggle_focus, desc = "Focus / unfocus current buffer" },
  { lhs = "<leader>y", handler = user_yank_sequence, desc = "Yank this window's sequence" },
  { lhs = "D",         handler = user_debug_dump,    desc = "Dump replay state to :messages" },
  { lhs = "q",         handler = cleanup_multi_sim,  desc = "Close replay" },
}, {
  title = "Vimfy Replay Keys",
  docs = true,
  settings = {
    lhs = "gs",
    handler = user_open_settings,
    desc = "Open play settings",
  },
})

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
---@param tokens VF.Sequence.Token[]
---@param lines string[]
---@param row integer  -- 0-indexed
---@param col integer  -- 0-indexed
---@param should_cancel fun(): boolean
---@param on_done fun(states: VF.Replay.Snapshot[])
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

  ---@type VF.Replay.Snapshot[]
  local states = {}

  ---@param state VF.Replay.Snapshot
  local function apply_probe_snapshot(state)
    if not v.nvim_win_is_valid(probe_win) then return end
    v.nvim_set_current_win(probe_win)
    v.nvim_buf_set_lines(buf, 0, -1, true, state.lines)
    local safe_row = max(1, min(state.cursor[1], #state.lines))
    local line = state.lines[safe_row] or ""
    local safe_col = max(0, min(state.cursor[2], max(0, #line - 1)))
    v.nvim_win_set_cursor(probe_win, { safe_row, safe_col })
  end

  ---@param state VF.Replay.Snapshot
  local function restore_snapshot(state)
    if not v.nvim_win_is_valid(probe_win) then return end
    v.nvim_set_current_win(probe_win)
    pcall(cmd, "stopinsert")
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

  ---@param token_text string
  ---@return string
  local function bare_command(token_text)
    return token_text:gsub("^%d+", "")
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

    -- Headless feedkeys can leave `c{motion}` queued until text arrives.
    -- Replay edit prefixes from the pre-edit checkpoint instead.
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
      vim.notify("vimfy precompute failed: " .. tostring(err),
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
  v.nvim_buf_set_name(buf, "vimfy-sim-" .. label:gsub("%s+", "_"):sub(1, 20))
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
    desc = "vimfy: refresh focus indicator on window enter",
  })

  return buf
end

--- Apply replay window-local options. Replay panes are read-only views of
--- precomputed snapshots — they should never inherit user chrome (line
--- numbers, sign column, fold column, color column) that makes them look
--- like editable buffers.
---@param win integer
local function decorate_sim_window(win)
  util.configure_scratch_window(win, {
    cursorline   = true,
    cursorcolumn = true,
  })
end

--- Attach a buffer to a window, clamp the cursor, enable focus indicators.
---@param win integer
---@param buf integer
---@param lines string[]
---@param row integer 0-indexed
---@param col integer 0-indexed
local function setup_sim_window(win, buf, lines, row, col)
  v.nvim_win_set_buf(win, buf)

  local line_count = #lines
  local safe_row = max(1, min(row + 1, line_count))
  local line_len = #(lines[safe_row] or "")
  local safe_col = max(0, min(col, max(0, line_len - 1)))
  v.nvim_win_set_cursor(win, { safe_row, safe_col })

  decorate_sim_window(win)
end

---@class VF.Replay.LayoutSlot
---@field is_user boolean
---@field default_rank integer  -- suggestion rank for non-user panes; 0 for user pane

--- Compute the effective layout (number of panes, whether to include user)
--- from play settings, trimmed to what the pool actually contains.
---@return integer window_count
---@return boolean include_user
local function effective_layout()
  local settings = require("vimficiency.play").get_settings()
  local include_user = (settings.include_user_sequence == true)
                       and multi_sim.pool.user ~= nil
  local window_count = max(1, min(4, settings.window_count or 2))
  local suggestion_slots = window_count - (include_user and 1 or 0)
  local total_suggestions = #multi_sim.pool.suggestions
  if suggestion_slots > total_suggestions then
    suggestion_slots = total_suggestions
    window_count = suggestion_slots + (include_user and 1 or 0)
  end
  if window_count < 1 then window_count = 1 end
  return window_count, include_user
end

--- Slots for the current effective layout, leftmost first. User pane (if
--- any) is always index 1.
---@return VF.Replay.LayoutSlot[]
---@diagnostic disable-next-line: redefined-local
build_layout_plan = function()
  local window_count, include_user = effective_layout()
  ---@type VF.Replay.LayoutSlot[]
  local plan = {}
  if include_user then
    plan[#plan + 1] = { is_user = true, default_rank = 0 }
  end
  local suggestion_slots = window_count - (include_user and 1 or 0)
  for rank = 1, suggestion_slots do
    plan[#plan + 1] = { is_user = false, default_rank = rank }
  end
  return plan
end

--- Tokenize the pool from the raw items passed into simulate_compare.
--- Stores results under `multi_sim.pool`; states are left nil until
--- `ensure_states_for_ranks()` fills them.
---@param pool_arg { user: { seq: string, cost: string? }?, suggestions: { seq: string, cost: string? }[] }
local function build_pool(pool_arg)
  multi_sim.pool = { user = nil, suggestions = {} }
  if pool_arg.user then
    multi_sim.pool.user = {
      tokens = tokenize_for_animation(pool_arg.user.seq),
      cost = pool_arg.user.cost,
      states = nil,
    }
  end
  for i, sug in ipairs(pool_arg.suggestions or {}) do
    multi_sim.pool.suggestions[i] = {
      tokens = tokenize_for_animation(sug.seq),
      cost = sug.cost,
      states = nil,
    }
  end
end

--- Rank key used when asking `ensure_states_for_ranks` to cache entries.
--- Either the string `"user"` or a 1-indexed suggestion rank.
---@alias VF.Replay.PoolRankKey "user" | integer

--- Resolve a rank key to a pool entry.
---@param key VF.Replay.PoolRankKey
---@return VF.Replay.PoolEntry?
local function pool_entry_by_rank(key)
  if key == "user" then return multi_sim.pool.user end
  return multi_sim.pool.suggestions[key]
end

--- Ensure states are precomputed for every referenced rank, then invoke
--- cb. Runs serially, reuses cached states, and bails if the precompute
--- generation rolls over mid-flight.
---@param ranks VF.Replay.PoolRankKey[]
---@param cb fun()
---@diagnostic disable-next-line: redefined-local
ensure_states_for_ranks = function(ranks, cb)
  local my_gen = multi_sim.precompute_gen
  local function step(i)
    if multi_sim.precompute_gen ~= my_gen then return end
    if i > #ranks then cb(); return end
    local pool_entry = pool_entry_by_rank(ranks[i])
    if not pool_entry or pool_entry.states then
      step(i + 1)
      return
    end
    precompute_states(
      pool_entry.tokens,
      multi_sim.source_lines or {},
      multi_sim.source_row or 0,
      multi_sim.source_col or 0,
      function() return multi_sim.precompute_gen ~= my_gen end,
      function(states)
        if multi_sim.precompute_gen ~= my_gen then return end
        pool_entry.states = states
        step(i + 1)
      end)
  end
  step(1)
end

--- Build the replay tab after precompute completes.
---@param lines string[]
---@param row integer
---@param col integer
---@param plan VF.Replay.LayoutSlot[]
local function build_sim_ui(lines, row, col, plan)
  cmd("tabnew")
  multi_sim.sim_tab = v.nvim_get_current_tabpage()
  local tabnew_buf = v.nvim_get_current_buf()

  -- Up to 4 panes in a single row. `vsplit` consistently splits the
  -- current window horizontally; wincmd = equalizes at the end.
  for i, slot in ipairs(plan) do
    local label = slot.is_user and "you" or tostring(slot.default_rank)
    local buf = create_sim_buffer(lines, label)

    ---@type integer
    local win
    if i == 1 then
      win = v.nvim_get_current_win()
    else
      cmd("vsplit")
      win = v.nvim_get_current_win()
    end

    setup_sim_window(win, buf, lines, row, col)
    ---@type VF.Replay.Window
    local replay_win = {
      win          = win,
      buf          = buf,
      is_user      = slot.is_user,
      seq_idx      = slot.is_user and 0 or slot.default_rank,
      default_rank = slot.default_rank,
    }
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
    local pool_entry = pool_entry_for(entry)
    if pool_entry and pool_entry.states and #pool_entry.states > 0 then
      apply_state(entry, pool_entry.states[1])
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
    vim.notify("vimfy: already focused; use :Vimfy escape first",
      vim.log.levels.WARN)
    return
  end
  if #multi_sim.windows == 0 then
    vim.notify("vimfy: no replay in progress", vim.log.levels.WARN)
    return
  end
  if not idx or idx < 1 or idx > #multi_sim.windows then
    vim.notify(
      "vimfy: focus index must be 1.." .. #multi_sim.windows,
      vim.log.levels.ERROR)
    return
  end

  ---@type VF.Replay.FocusSavedEntry[]
  local saved_entries = {}
  for i, entry in ipairs(multi_sim.windows) do
    saved_entries[i] = {
      buf          = entry.buf,
      is_user      = entry.is_user,
      seq_idx      = entry.seq_idx,
      default_rank = entry.default_rank,
    }
    v.nvim_set_option_value("bufhidden", "hide", { buf = entry.buf })
  end

  local focused = multi_sim.windows[idx]
  local focus_win = focused.win
  v.nvim_set_current_win(focus_win)
  cmd("only")

  multi_sim.windows = { {
    win          = focus_win,
    buf          = focused.buf,
    is_user      = focused.is_user,
    seq_idx      = focused.seq_idx,
    default_rank = focused.default_rank,
  } }
  focus_state = { saved_entries = saved_entries, focused_idx = idx }
  refresh()
end

--- Restore the split replay layout.
user_escape = function()
  if not focus_state then
    vim.notify("vimfy: not currently focused", vim.log.levels.INFO)
    return
  end
  local saved_entries = focus_state.saved_entries
  local focused_idx = focus_state.focused_idx

  local current_win = v.nvim_get_current_win()
  local first = saved_entries[1]
  v.nvim_win_set_buf(current_win, first.buf)
  decorate_sim_window(current_win)

  ---@type VF.Replay.Window[]
  local new_windows = { {
    win          = current_win,
    buf          = first.buf,
    is_user      = first.is_user,
    seq_idx      = first.seq_idx,
    default_rank = first.default_rank,
  } }
  for i = 2, #saved_entries do
    cmd("vsplit")
    local new_win = v.nvim_get_current_win()
    local saved = saved_entries[i]
    v.nvim_win_set_buf(new_win, saved.buf)
    decorate_sim_window(new_win)
    new_windows[i] = {
      win          = new_win,
      buf          = saved.buf,
      is_user      = saved.is_user,
      seq_idx      = saved.seq_idx,
      default_rank = saved.default_rank,
    }
  end

  -- Restore normal replay teardown behavior.
  for _, saved in ipairs(saved_entries) do
    if v.nvim_buf_is_valid(saved.buf) then
      v.nvim_set_option_value("bufhidden", "wipe", { buf = saved.buf })
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

--- Simulate sequences side-by-side in a new tab.
--- Replay snapshots are precomputed through the hidden Neovim probe window.
---@class VF.Replay.Opts
---@field label   string?   Display label shown in the replay statusline.
---@field end_row integer?  0-indexed end row of the captured session.
---@field end_col integer?  0-indexed end col.
---@field initial_window_count integer?  One-shot override from `:Vimfy play <alias> <N>`.
                                      -- Persisted to the settings store so later
                                      -- relayouts inherit it.

---@class VF.Replay.PoolArg
---@field user { seq: string, cost: string? }?       The user's typed sequence; leftmost pane when shown.
---@field suggestions { seq: string, cost: string? }[]  Optimal-result sequences, best-first.

---@param lines string[] Buffer content to simulate on
---@param row integer 0-indexed starting row
---@param col integer 0-indexed starting column
---@param pool_arg VF.Replay.PoolArg Sequence pool (user + all suggestions).
---@param opts VF.Replay.Opts? Optional display extras.
function M.simulate_compare(lines, row, col, pool_arg, opts)
  if #multi_sim.windows > 0 then
    cleanup_multi_sim()
  end
  if not pool_arg
     or (not pool_arg.user and #(pool_arg.suggestions or {}) == 0) then
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
  multi_sim.source_lines = vim.deepcopy(lines)
  multi_sim.source_row = row
  multi_sim.source_col = col
  multi_sim.source_opts = {
    label = opts.label,
    end_row = opts.end_row,
    end_col = opts.end_col,
  }
  multi_sim.saved_win = v.nvim_get_current_win()
  multi_sim.saved_tab = v.nvim_get_current_tabpage()
  multi_sim.windows = {}
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  local my_gen = multi_sim.precompute_gen

  -- One-shot CLI override (`:Vimfy play <alias> <N>`). Persist it so the
  -- next relayout inherits the same count — matches the user's mental
  -- model that +/-/modal toggles and CLI args go through the same store.
  if opts.initial_window_count then
    require("vimficiency.play").set_setting(
      "window_count", max(1, min(4, opts.initial_window_count)))
  end

  build_pool(pool_arg)

  local plan = build_layout_plan()
  local ranks = {}
  for _, slot in ipairs(plan) do
    ranks[#ranks + 1] = slot.is_user and "user" or slot.default_rank
  end

  vim.notify("vimfy: precomputing replay…", vim.log.levels.INFO)

  ensure_states_for_ranks(ranks, function()
    if multi_sim.precompute_gen ~= my_gen then return end
    build_sim_ui(lines, row, col, plan)
  end)
end

--- Teardown windows + sim tab, keeping `multi_sim.pool` and source
--- coordinates intact. Used by `relayout_grid()` when the user toggles
--- window_count / include_user — rebuilding the grid shouldn't drop the
--- precomputed states we already have.
local function teardown_ui_keep_pool()
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  destroy_status_bar()

  if focus_state then
    for _, saved in ipairs(focus_state.saved_entries) do
      if v.nvim_buf_is_valid(saved.buf) then
        v.nvim_buf_delete(saved.buf, { force = true })
      end
    end
    focus_state = nil
  end

  for _, entry in ipairs(multi_sim.windows) do
    if v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_delete(entry.buf, { force = true })
    end
  end
  multi_sim.windows = {}

  if multi_sim.sim_tab and v.nvim_tabpage_is_valid(multi_sim.sim_tab) then
    if multi_sim.saved_tab and v.nvim_tabpage_is_valid(multi_sim.saved_tab) then
      v.nvim_set_current_tabpage(multi_sim.saved_tab)
    end
    local sim_tab_nr = v.nvim_tabpage_get_number(multi_sim.sim_tab)
    pcall(function() cmd("tabclose " .. sim_tab_nr) end)
  end
  multi_sim.sim_tab = nil
end

--- Rebuild the grid using current play settings. Reuses cached states in
--- `multi_sim.pool`; only newly-visible ranks trigger fresh precompute.
---@return boolean rebuilt  false when there's no active session to relayout
function M.relayout_grid()
  if not multi_sim.source_lines then return false end
  local resume_step = multi_sim.global_step

  teardown_ui_keep_pool()

  local plan = build_layout_plan()
  local ranks = {}
  for _, slot in ipairs(plan) do
    ranks[#ranks + 1] = slot.is_user and "user" or slot.default_rank
  end

  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  local my_gen = multi_sim.precompute_gen

  ensure_states_for_ranks(ranks, function()
    if multi_sim.precompute_gen ~= my_gen then return end
    build_sim_ui(multi_sim.source_lines, multi_sim.source_row or 0,
                 multi_sim.source_col or 0, plan)
    if resume_step > 0 then
      seek_to(resume_step)
    end
  end)
  return true
end

M.cleanup_compare = cleanup_multi_sim
M.focus           = user_focus
M.escape          = user_escape
M.open_settings   = user_open_settings

-- Debug-only accessors (not part of the public API). Let tests peek at
-- internal state without parsing winbars or tabs. The states/sequences
-- arrays are synthesized pane-indexed views over `multi_sim.pool` so
-- existing tests (pane-indexed lookups) keep working after the pool
-- refactor.

M._debug_get_states = function()
  local out = {}
  for i, entry in ipairs(multi_sim.windows) do
    local pe = pool_entry_for(entry)
    out[i] = pe and pe.states or nil
  end
  return out
end
M._debug_get_sequences = function()
  local out = {}
  for i, entry in ipairs(multi_sim.windows) do
    local pe = pool_entry_for(entry)
    out[i] = pe and pe.tokens or nil
  end
  return out
end
M._debug_get_windows = function() return multi_sim.windows end
M._debug_get_pool = function() return multi_sim.pool end
M._debug_get_focus_state = function() return focus_state end
M._debug_seek_to = seek_to
M._debug_toggle_focus = user_toggle_focus
M._debug_cycle_next = user_cycle_next
M._debug_cycle_prev = user_cycle_prev
M._debug_cycle_rank_next = function() user_cycle_rank(1) end
M._debug_cycle_rank_prev = function() user_cycle_rank(-1) end
M._debug_tokenize_for_animation = tokenize_for_animation

return M
