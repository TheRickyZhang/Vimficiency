-- lua/vimficiency/simulate.lua
local v   = vim.api
local cmd = vim.cmd

local M = {}

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
local function simulate_immediate(buf, win, starting_lines, row, col, keys)
  local prev_win = v.nvim_get_current_win()

  v.nvim_buf_set_lines(buf, 0, -1, true, starting_lines)
  v.nvim_win_set_cursor(win, { row, col })
  v.nvim_set_current_win(win)

  cmd("normal! " .. keys)

  local new_lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  local new_cursor = v.nvim_win_get_cursor(win)

  v.nvim_set_current_win(prev_win)

  return {
    lines  = new_lines,
    cursor = { new_cursor[1], new_cursor[2] },
    buf    = buf,
    win    = win,
  }
end

--- Internal: incremental simulation using timer + feedkeys
local function simulate_incremental(buf, win, starting_lines, row, col, keys, delay_ms)
  delay_ms = delay_ms or 500

  -- Convert key-notation (<Esc>, <CR>, etc) into real termcodes once
  local termkeys = v.nvim_replace_termcodes(keys, true, false, true)

  local prev_win = v.nvim_get_current_win()

  v.nvim_buf_set_lines(buf, 0, -1, true, starting_lines)
  v.nvim_win_set_cursor(win, { row, col })

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

    -- Send exactly one byte (one “keypress” unit) at a time
    local chunk = string.sub(termkeys, i, i)
    i = i + 1

    -- Temporarily focus sim window, feed key, then restore user's window
    local cur_prev = v.nvim_get_current_win()
    v.nvim_set_current_win(win)
    v.nvim_feedkeys(chunk, "n", false) -- non-remap
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
--- @param row integer       1-based
--- @param col integer       0-based
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

