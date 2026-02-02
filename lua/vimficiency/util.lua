local M = {}

local v  = vim.api
local uv = vim.uv
local fs = vim.fs

local config = require("vimficiency.config")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

---@class VimficiencyState
---@field bufname string       # original buffer name (full path)
---@field filetype string      # buffer filetype
---@field row integer          # 0-indexed cursor row
---@field col integer          # 0-indexed cursor column
---@field top_row integer      # top visible row
---@field bottom_row integer   # bottom visible row
---@field window_height integer # Ctrl-F, B distance
---@field scroll_amount integer # Ctrl-D, U distance (may differ from window_height/2)
---@field lines string[]       # buffer lines at capture time

--------------------------------------------------------------------------------
-- State Constructor
--------------------------------------------------------------------------------

---@param bufname string
---@param filetype string
---@param row integer
---@param col integer
---@param top_row integer
---@param bottom_row integer
---@param window_height integer
---@param scroll_amount integer
---@param lines string[]
---@return VimficiencyState
local function new_state(bufname, filetype, row, col, top_row, bottom_row, window_height, scroll_amount, lines)
  assert(type(bufname) == "string", "state.bufname must be string")
  assert(type(filetype) == "string", "state.filetype must be string")
  assert(type(row) == "number" and type(col) == "number", "row and col must be numbers")
  assert(type(lines) == "table", "state.lines must be an array of strings")

  return {
    bufname = bufname,
    filetype = filetype,
    row = row,
    col = col,
    top_row = top_row,
    bottom_row = bottom_row,
    window_height = window_height,
    scroll_amount = scroll_amount,
    lines = lines,
  }
end

--------------------------------------------------------------------------------
-- Utility Functions
--------------------------------------------------------------------------------
M.basename = fs.basename or function(p)
  return p:match("([^/\\]+)$") or p
end

function M.ensure_dir(p)
  vim.fn.mkdir(p, "p")
end

-- ID Generation for file names (Snowflake method)
local function sanitize_name(s)
  return (s:gsub("[^%w%._%-]", "_"))
end

function M.find_plugin_root()
    -- This file is at: <plugin_root>/lua/vimficiency/util.lua, so go up 3 levels.
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)  -- remove leading @
    end
    return vim.fn.fnamemodify(source, ":h:h:h")
end

---@param title string
---@param text? string
---@return integer buf
---@return integer win
function M.show_output(title, text)
  local lines = { title }
  if text and text ~= "" then
    lines[#lines + 1] = ""
    vim.list_extend(lines, vim.split(text, "\n", { plain = true }))
  end

  vim.cmd("botright new") -- create split + window
  local win = v.nvim_get_current_win()

  local buf = v.nvim_create_buf(false, true) -- listed=false, scratch=true
  v.nvim_win_set_buf(win, buf)

  v.nvim_buf_set_name(buf, ("vimficiency://%s"):format(title:gsub("%s+", "_")))
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)

  -- Modern option APIs
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false

  vim.wo[win].number = false
  vim.wo[win].relativenumber = false
  vim.wo[win].wrap = false
  vim.wo[win].signcolumn = "no"

  return buf, win
end


-- Snowflake-like: wall-clock milliseconds + per-ms sequence
local last_ms = -1
local seq = 0
local SEQ_BITS = 12
local SEQ_MAX = (2 ^ SEQ_BITS) - 1

local function now_ms()
  local sec, usec = uv.gettimeofday()
  local ms = sec * 1000 + math.floor(usec / 1000)
  return ms, sec
end

local function wait_next_ms(prev_ms)
  local ms, sec = now_ms()
  while ms == prev_ms do
    ms, sec = now_ms()
  end
  return ms, sec
end

function M.new_id(buf)
  assert(type(buf) == "number", "buf must be a number")
  assert(v.nvim_buf_is_valid(buf), "invalid buffer")

  local name = v.nvim_buf_get_name(buf)
  local base = sanitize_name(
    (name ~= "" and M.basename(name)) or "NoName"
  )

  local ms, sec = now_ms()

  -- Clamp if clock goes backwards
  if ms < last_ms then
    ms = last_ms
    sec = math.floor(ms / 1000)
  end

  if ms == last_ms then
    -- Same millisecond as previous id
    if seq < SEQ_MAX then
      seq = seq + 1
    else
      -- Overflow: wait for next millisecond
      ms, sec = wait_next_ms(last_ms)
      last_ms = ms
      seq = 0
    end
  else
    -- New millisecond
    last_ms = ms
    seq = 0
  end

  local wall = os.date("%Y%m%d-%H%M%S", sec)
    .. string.format("-%03d", ms % 1000)

  -- Example: foo_cpp__20251222-153045-123__1734888645123__0001__b3
  return string.format("%s__%s__%d__%04d__b%d", base, wall, ms, seq, buf)
end
----------- BEGIN FILE ------------


---------- State / IO helpers ----------
---@return VimficiencyState
function M.capture_state(buf, win)
  -- Precondition: must pass in valid buffer and window
  assert(buf and win, "capture_state: buf and win required")
  assert(v.nvim_buf_is_valid(buf), "capture state: invalid buffer")
  assert(v.nvim_win_is_valid(win), "capture state: invalid window")
  assert(v.nvim_win_get_buf(win) == buf, "capture state: buffer not in window")

  local lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  local cursor = v.nvim_win_get_cursor(win) -- {row(1-based), col(0-based)}
  local top_row = vim.fn.line('w0')
  local bottom_row = vim.fn.line('w$')
  local window_height = vim.api.nvim_win_get_height(win)
  local scroll_amount = vim.api.nvim_get_option_value('scroll', {win=win})

  return new_state(
    v.nvim_buf_get_name(buf),
    vim.bo[buf].filetype,
    cursor[1] - 1, -- Convert to 0-indexed
    cursor[2],
    top_row,
    bottom_row,
    window_height,
    scroll_amount,
    lines
  )
end

function M.get_search_boundaries(begin_row, end_row)
  assert(config, "config module not loaded")
  local padding = config.SLICE_PADDING

  if begin_row > end_row then
    begin_row, end_row = end_row, begin_row
  end

  local buf = 0
  local nlines = vim.api.nvim_buf_line_count(buf) -- count, not last index

  local start_search = math.max(0, begin_row - padding)
  local end_search = math.min(nlines-1, end_row + padding)

  if config.SLICE_EXPAND_TO_PARAGRAPH then
    local function is_blank_line(r)
      local l = vim.api.nvim_buf_get_lines(buf, r, r + 1, false)[1] or ""
      return l:match("^%s*$") ~= nil
    end
    while start_search > 0 and not is_blank_line(start_search - 1) do
      start_search = start_search - 1
    end
    while end_search < nlines and not is_blank_line(end_search) do
      end_search = end_search + 1
    end
  end

  return start_search, end_search, nlines
end

function M.check_state_inconsistencies(start_state, end_state)
  if end_state.scroll_amount ~= start_state.scroll_amount then
    vim.notify("scroll amount changed during session")
  end
  if end_state.window_height ~= start_state.window_height then
    vim.notify("window height changed during session")
  end
end

--- Find the first and last differing lines between two line arrays
---@param a string[] First line array
---@param b string[] Second line array
---@return integer|nil first_diff 0-indexed first differing line, or nil if equal
---@return integer|nil last_diff 0-indexed last differing line, or nil if equal
function M.find_diff_range(a, b)
  local first_diff, last_diff = nil, nil
  local max_len = math.max(#a, #b)
  for i = 1, max_len do
    if a[i] ~= b[i] then
      if not first_diff then first_diff = i - 1 end
      last_diff = i - 1
    end
  end
  return first_diff, last_diff
end

--- Compute the search region that covers cursor positions and changed lines
--- Returns (region_start, region_end) as 0-indexed line numbers
---@param start_row integer 0-indexed start cursor row
---@param end_row integer 0-indexed end cursor row
---@param start_lines string[] Buffer lines at session start
---@param end_lines string[] Buffer lines at session end
---@param padding integer Number of lines to add above/below
---@return integer region_start 0-indexed first line of region
---@return integer region_end 0-indexed last line of region
function M.compute_search_region(start_row, end_row, start_lines, end_lines, padding)
  local min_row = math.min(start_row, end_row)
  local max_row = math.max(start_row, end_row)

  -- Find changed line range (first/last differing line)
  local first_diff, last_diff = M.find_diff_range(start_lines, end_lines)
  if first_diff then
    min_row = math.min(min_row, first_diff)
    max_row = math.max(max_row, last_diff)
  end

  -- Apply padding, clamp to buffer bounds
  -- Use max of both buffer lengths since edits may add/remove lines
  local buffer_len = math.max(#start_lines, #end_lines)
  local region_start = math.max(0, min_row - padding)
  local region_end = math.min(buffer_len - 1, max_row + padding)

  return region_start, region_end
end

return M
