-- lua/vimficiency/simulate.lua
local v   = vim.api
local cmd = vim.cmd
local ffi_lib = require("vimficiency.ffi")

local M = {}

-- =============================================================================
-- Multi-sequence comparison state
-- =============================================================================

local multi_sim = {
  active = false,
  timer = nil,
  windows = {},    -- { win, buf }[]
  sequences = {},  -- parsed motion tokens per sequence
  indices = {},    -- current step index per sequence
  orig_win = nil,
}

-- =============================================================================
-- Multi-sequence comparison functions
-- =============================================================================

--- Clean up all multi-sim windows and state
local function cleanup_multi_sim()
  if multi_sim.timer then
    multi_sim.timer:stop()
    multi_sim.timer:close()
    multi_sim.timer = nil
  end

  for _, entry in ipairs(multi_sim.windows) do
    if entry.win and v.nvim_win_is_valid(entry.win) then
      v.nvim_win_close(entry.win, true)
    end
    if entry.buf and v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_delete(entry.buf, { force = true })
    end
  end

  if multi_sim.orig_win and v.nvim_win_is_valid(multi_sim.orig_win) then
    v.nvim_set_current_win(multi_sim.orig_win)
  end

  multi_sim.active = false
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.indices = {}
  multi_sim.orig_win = nil
end

--- Create a simulation window with buffer
---@param lines string[]
---@param row integer 0-indexed
---@param col integer 0-indexed
---@param label string
---@return { win: integer, buf: integer }
local function create_sim_window(lines, row, col, label)
  local buf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_name(buf, "vimficiency-sim-" .. label:gsub("%s+", "_"):sub(1, 20))
  v.nvim_set_option_value("buftype", "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
  v.nvim_set_option_value("swapfile", false, { buf = buf })
  v.nvim_set_option_value("modifiable", true, { buf = buf })

  v.nvim_buf_set_lines(buf, 0, -1, true, lines)

  -- 'q' to quit all simulation windows
  v.nvim_buf_set_keymap(buf, "n", "q", "", {
    callback = cleanup_multi_sim,
    noremap = true,
    silent = true,
    desc = "Close all simulation windows",
  })

  cmd("vsplit")
  local win = v.nvim_get_current_win()
  v.nvim_win_set_buf(win, buf)

  -- Clamp row to valid range and convert to 1-indexed for Neovim API
  local line_count = #lines
  local nvim_row = row + 1  -- Convert 0-indexed to 1-indexed
  local safe_row = math.max(1, math.min(nvim_row, line_count))
  local line_len = #(lines[safe_row] or "")
  local safe_col = math.max(0, math.min(col, math.max(0, line_len - 1)))

  v.nvim_win_set_cursor(win, { safe_row, safe_col })

  -- Show label in winbar if available
  pcall(function()
    v.nvim_set_option_value("winbar", label, { win = win })
  end)

  return { win = win, buf = buf }
end

--- Advance all sequences by one step
local function step_all()
  local all_done = true

  for i, entry in ipairs(multi_sim.windows) do
    local idx = multi_sim.indices[i]
    local tokens = multi_sim.sequences[i]

    if idx <= #tokens and v.nvim_win_is_valid(entry.win) then
      all_done = false
      local token = tokens[idx]
      -- Convert <C-d> style notation to actual key codes
      local termcodes = v.nvim_replace_termcodes(token, true, false, true)

      local prev = v.nvim_get_current_win()
      v.nvim_set_current_win(entry.win)
      cmd("normal! " .. termcodes)
      v.nvim_set_current_win(prev)

      multi_sim.indices[i] = idx + 1
    end
  end

  if all_done then
    if multi_sim.timer then
      multi_sim.timer:stop()
    end
    -- Don't close windows - let user inspect final positions
  end
end

--- Simulate multiple motion sequences side-by-side for comparison
---@param lines string[] Buffer content to simulate on
---@param row integer 0-indexed starting row
---@param col integer 0-indexed starting column
---@param sequences string[] Array of motion sequences (e.g., {"3w", "wwwfa;", "jjjw"})
---@param delay_ms integer|nil Delay between steps in milliseconds (default 300)
function M.simulate_compare(lines, row, col, sequences, delay_ms)
  delay_ms = delay_ms or 300

  if multi_sim.active then
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

  multi_sim.active = true
  multi_sim.orig_win = v.nvim_get_current_win()
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.indices = {}

  -- Parse sequences and create windows (in reverse order so first seq is leftmost)
  for i = #sequences, 1, -1 do
    local seq = sequences[i]
    local tokens, err = ffi_lib.tokenize_motions(seq)
    -- If tokenization failed (e.g., user sequence with custom mappings), treat whole seq as one token
    if err or #tokens == 0 then
      tokens = { seq }
    end
    multi_sim.sequences[i] = tokens
    multi_sim.indices[i] = 1

    local label = string.format("[%d] %s", i, seq)
    local entry = create_sim_window(lines, row, col, label)
    table.insert(multi_sim.windows, 1, entry)  -- prepend to maintain order
  end

  -- Focus first simulation window
  if #multi_sim.windows > 0 and v.nvim_win_is_valid(multi_sim.windows[1].win) then
    v.nvim_set_current_win(multi_sim.windows[1].win)
  end

  -- Start animation timer
  multi_sim.timer = vim.loop.new_timer()
  multi_sim.timer:start(delay_ms, delay_ms, vim.schedule_wrap(step_all))
end

-- Export cleanup for external use
M.cleanup_compare = cleanup_multi_sim

-- Dedicated scratch buffer/window used for simulations
local sim_buf = nil
local sim_win = nil

local function ensure_sim_buf()
  if sim_buf and v.nvim_buf_is_valid(sim_buf) then
    return sim_buf
  end

  local buf = v.nvim_create_buf(false, true) -- listed=false, scratch=true
  v.nvim_buf_set_name(buf, "vimficiency-sim")
  v.nvim_set_option_value("buftype",   "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "hide",   { buf = buf })
  v.nvim_set_option_value("swapfile",  false,    { buf = buf })

  sim_buf = buf
  return buf
end

local function ensure_sim_win(buf, split)
  if sim_win and v.nvim_win_is_valid(sim_win) and v.nvim_win_get_buf(sim_win) == buf then
    return sim_win
  end

  local prev_win = v.nvim_get_current_win()

  if split == "h" then
    cmd("split")
  else
    cmd("vsplit")
  end

  local win = v.nvim_get_current_win()
  v.nvim_win_set_buf(win, buf)

  -- Go back to whatever window the user was in
  v.nvim_set_current_win(prev_win)

  sim_win = win
  return win
end

--- Internal: immediate simulation (no delay, everything applied at once)
---@param row integer 0-indexed
---@param col integer 0-indexed
local function simulate_immediate(buf, win, starting_lines, row, col, keys)
  local prev_win = v.nvim_get_current_win()

  v.nvim_buf_set_lines(buf, 0, -1, true, starting_lines)
  v.nvim_win_set_cursor(win, { row + 1, col })  -- Convert to 1-indexed for Neovim
  v.nvim_set_current_win(win)

  cmd("normal! " .. keys)

  local new_lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  local new_cursor = v.nvim_win_get_cursor(win)

  v.nvim_set_current_win(prev_win)

  return {
    lines  = new_lines,
    cursor = { new_cursor[1] - 1, new_cursor[2] },
    buf    = buf,
    win    = win,
  }
end

--- Internal: incremental simulation using timer + feedkeys
---@param row integer 0-indexed
---@param col integer 0-indexed
local function simulate_incremental(buf, win, starting_lines, row, col, keys, delay_ms)
  delay_ms = delay_ms or 500

  -- Convert key-notation (<Esc>, <CR>, etc) into real termcodes once
  local termkeys = v.nvim_replace_termcodes(keys, true, false, true)

  local prev_win = v.nvim_get_current_win()

  v.nvim_buf_set_lines(buf, 0, -1, true, starting_lines)
  v.nvim_win_set_cursor(win, { row + 1, col })  -- Convert to 1-indexed for Neovim

  -- We want the user to stay in their original window while the sim runs
  v.nvim_set_current_win(prev_win)

  local i, n = 1, #termkeys

  local function step()
    if not (v.nvim_buf_is_valid(buf) and v.nvim_win_is_valid(win)) then
      -- Simulation window/buffer got killed; give up quietly.
      return
    end
    if i > n then
      -- Done: nothing more to do. Buffer/window stay visible for inspection.
      return
    end

    -- Send exactly one byte (one "keypress" unit) at a time
    local chunk = string.sub(termkeys, i, i)
    i = i + 1

    -- Temporarily focus sim window, execute key synchronously, then restore
    local cur_prev = v.nvim_get_current_win()
    v.nvim_set_current_win(win)
    cmd("normal! " .. chunk)
    v.nvim_set_current_win(cur_prev)

    -- Schedule next key
    vim.defer_fn(step, delay_ms)
  end

  -- Kick off the first step
  vim.defer_fn(step, delay_ms)

  -- Nothing meaningful to return here; the simulation is async.
  return nil
end

--- Public API: simulate a key sequence in a dedicated scratch buffer.
--- If delay_ms == 0 or nil  -> immediate (everything at once, returns final state)
--- If delay_ms > 0          -> incremental playback; returns nil.
---
--- @param starting_lines string[]
--- @param row integer       0-indexed
--- @param col integer       0-indexed
--- @param keys string       key sequence (normal-mode style, e.g. "gg=G", "ciw", etc.)
--- @param delay_ms? integer  optional
--- @param split? "h" | "v"   optional
function M.simulate_visible(starting_lines, row, col, keys, delay_ms, split)
  split = split or "h"
  delay_ms = delay_ms or 0

  local buf = ensure_sim_buf()
  local win = ensure_sim_win(buf, split)

  if delay_ms <= 0 then
    return simulate_immediate(buf, win, starting_lines, row, col, keys)
  else
    return simulate_incremental(buf, win, starting_lines, row, col, keys, delay_ms)
  end
end

return M

