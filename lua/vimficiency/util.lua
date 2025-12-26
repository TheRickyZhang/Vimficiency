local M = {}

local v  = vim.api
local uv = vim.uv
local fs = vim.fs

local types = require("vimficiency.types")

----------- BEGIN FILE ------------
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


-- ---------- State / IO helpers ----------
---@return VimficiencyState
function M.capture_state(buf, win)
  -- Precondition: must pass in valid buffer and window
  assert(buf and win, "capture_state: buf and win required")
  assert(v.nvim_buf_is_valid(buf), "capture state: invalid buffer")
  assert(v.nvim_win_is_valid(win), "capture state: invalid window")
  assert(v.nvim_win_get_buf(win) == buf, "capture state: buffer not in window")

  local lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  local cursor = v.nvim_win_get_cursor(win) -- {row(1-based), col(0-based)}

  return types.new_file_contents(
    v.nvim_buf_get_name(buf),
    vim.bo[buf].filetype,
    cursor[1] - 1, --- Convert to 0-indexed, annoyingly
    cursor[2],
    lines
  );
end


return M
