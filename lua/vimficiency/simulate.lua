-- lua/vimficiency/simulate.lua
local v   = vim.api
local cmd = vim.cmd
local ffi_lib = require("vimficiency.ffi")

local M = {}

-- Namespace for cursor highlight extmarks
local cursor_ns = v.nvim_create_namespace("vimficiency_cursor")

-- =============================================================================
-- Multi-sequence comparison state
-- =============================================================================

local multi_sim = {
  active = false,
  timer = nil,
  windows = {},    -- { win: integer, buf: integer }[]
  sequences = {},  -- parsed motion tokens per sequence
  indices = {},    -- current step index per sequence
  orig_win = nil,
  orig_tab = nil,
  sim_tab = nil,
}

-- =============================================================================
-- Multi-sequence comparison functions
-- =============================================================================

--- Update cursor highlight extmarks for all windows
local function update_cursor_highlights()
  for _, entry in ipairs(multi_sim.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      -- Clear previous highlights in this buffer
      v.nvim_buf_clear_namespace(entry.buf, cursor_ns, 0, -1)

      -- Get current cursor position (1-indexed)
      local cursor = v.nvim_win_get_cursor(entry.win)
      local row = cursor[1] - 1  -- Convert to 0-indexed for extmark
      local col = cursor[2]

      -- Add highlight at cursor position
      local line = v.nvim_buf_get_lines(entry.buf, row, row + 1, false)[1] or ""
      if col < #line then
        v.nvim_buf_set_extmark(entry.buf, cursor_ns, row, col, {
          end_col = col + 1,
          hl_group = "Cursor",
          priority = 1000,
        })
      elseif #line > 0 then
        -- Cursor at end of line, highlight last character
        v.nvim_buf_set_extmark(entry.buf, cursor_ns, row, #line - 1, {
          end_col = #line,
          hl_group = "Cursor",
          priority = 1000,
        })
      end
    end
  end
end

--- Clean up all multi-sim windows and state
local function cleanup_multi_sim()
  if multi_sim.timer then
    multi_sim.timer:stop()
    multi_sim.timer:close()
    multi_sim.timer = nil
  end

  -- Close the simulation tab if it exists
  if multi_sim.sim_tab and v.nvim_tabpage_is_valid(multi_sim.sim_tab) then
    -- Switch to original tab first
    if multi_sim.orig_tab and v.nvim_tabpage_is_valid(multi_sim.orig_tab) then
      v.nvim_set_current_tabpage(multi_sim.orig_tab)
    end
    -- Close sim tab (this also closes all windows/buffers in it)
    local sim_tab_nr = v.nvim_tabpage_get_number(multi_sim.sim_tab)
    pcall(cmd, "tabclose " .. sim_tab_nr)
  end

  -- Restore original window if possible
  if multi_sim.orig_win and v.nvim_win_is_valid(multi_sim.orig_win) then
    v.nvim_set_current_win(multi_sim.orig_win)
  end

  multi_sim.active = false
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.indices = {}
  multi_sim.orig_win = nil
  multi_sim.orig_tab = nil
  multi_sim.sim_tab = nil
end

--- Create a simulation buffer
---@param lines string[]
---@param row integer 0-indexed
---@param col integer 0-indexed
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

  -- 'q' to quit all simulation windows
  v.nvim_buf_set_keymap(buf, "n", "q", "", {
    callback = cleanup_multi_sim,
    noremap = true,
    silent = true,
    desc = "Close all simulation windows",
  })

  return buf
end

--- Set up a window with buffer and cursor position
---@param win integer
---@param buf integer
---@param lines string[]
---@param row integer 0-indexed
---@param col integer 0-indexed
---@param label string
local function setup_sim_window(win, buf, lines, row, col, label)
  v.nvim_win_set_buf(win, buf)

  -- Clamp row to valid range and convert to 1-indexed for Neovim API
  local line_count = #lines
  local nvim_row = row + 1  -- Convert 0-indexed to 1-indexed
  local safe_row = math.max(1, math.min(nvim_row, line_count))
  local line_len = #(lines[safe_row] or "")
  local safe_col = math.max(0, math.min(col, math.max(0, line_len - 1)))

  v.nvim_win_set_cursor(win, { safe_row, safe_col })

  -- Enable cursor indicators visible in unfocused windows
  v.nvim_set_option_value("cursorline", true, { win = win })
  v.nvim_set_option_value("cursorcolumn", true, { win = win })

  -- Show label in winbar if available
  pcall(function()
    v.nvim_set_option_value("winbar", label, { win = win })
  end)
end

--- Tokenize a sequence for animation, with character-by-character fallback
---@param seq string
---@return string[]
local function tokenize_for_animation(seq)
  -- First try the C++ tokenizer (works for optimizer-supported motions)
  local tokens, err = ffi_lib.tokenize_motions(seq)
  if not err and tokens and #tokens > 0 then
    return tokens
  end

  -- Fallback: split into individual characters for animation
  -- This handles user sequences with unsupported motions like gj, gk, etc.
  local chars = {}
  local i = 1
  while i <= #seq do
    -- Check for <...> style key notation
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
  return chars
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

  -- Update cursor highlights for all windows
  update_cursor_highlights()

  -- Force redraw to show updates
  cmd("redraw")

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
---@param delay_ms integer Delay between steps in milliseconds
function M.simulate_compare(lines, row, col, sequences, delay_ms)
  assert(delay_ms and delay_ms > 0, "delay_ms must be a positive integer")

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
  multi_sim.orig_tab = v.nvim_get_current_tabpage()
  multi_sim.windows = {}
  multi_sim.sequences = {}
  multi_sim.indices = {}

  -- Create a new tab for full-screen simulation
  cmd("tabnew")
  multi_sim.sim_tab = v.nvim_get_current_tabpage()

  -- Track the empty buffer created by tabnew so we can delete it
  local tabnew_buf = v.nvim_get_current_buf()

  -- Create buffers and windows for each sequence
  for i, seq in ipairs(sequences) do
    local tokens = tokenize_for_animation(seq)
    multi_sim.sequences[i] = tokens
    multi_sim.indices[i] = 1

    local label = string.format("[%d] %s", i, seq)
    local buf = create_sim_buffer(lines, label)

    local win
    if i == 1 then
      -- First window: use the current window from tabnew
      win = v.nvim_get_current_win()
    else
      -- Subsequent windows: create vertical splits
      cmd("vsplit")
      win = v.nvim_get_current_win()
    end

    setup_sim_window(win, buf, lines, row, col, label)
    table.insert(multi_sim.windows, { win = win, buf = buf })
  end

  -- Delete the empty buffer created by tabnew (it's no longer displayed)
  if v.nvim_buf_is_valid(tabnew_buf) then
    v.nvim_buf_delete(tabnew_buf, { force = true })
  end

  -- Equalize window sizes
  cmd("wincmd =")

  -- Focus first simulation window
  if #multi_sim.windows > 0 and v.nvim_win_is_valid(multi_sim.windows[1].win) then
    v.nvim_set_current_win(multi_sim.windows[1].win)
  end

  -- Set initial cursor highlights
  update_cursor_highlights()

  -- Start animation timer
  multi_sim.timer = vim.uv.new_timer()
  multi_sim.timer:start(delay_ms, delay_ms, vim.schedule_wrap(step_all))
end

-- Export cleanup for external use
M.cleanup_compare = cleanup_multi_sim

return M

