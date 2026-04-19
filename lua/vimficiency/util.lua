local M = {}

local v  = vim.api
local uv = vim.uv
local fs = vim.fs

local config = require("vimficiency.config")
local ffi_lib = require("vimficiency.ffi")

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

---@class VimficiencyBufferKeymap
---@field lhs string
---@field handler string|function
---@field desc string
---@field mode string|string[]?
---@field nowait boolean?
---@field silent boolean?
---@field expr boolean?
---@field remap boolean?

M.basename = fs.basename or function(p)
  return p:match("([^/\\]+)$") or p
end

function M.ensure_dir(p)
  vim.fn.mkdir(p, "p")
end

local function sanitize_name(s)
  return (s:gsub("[^%w%._%-]", "_"))
end

function M.find_plugin_root()
    -- This file lives at `<plugin_root>/lua/vimficiency/util.lua`.
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)  -- remove leading @
    end
    return vim.fn.fnamemodify(source, ":h:h:h")
end

---@param tag string
function M.open_help(tag)
  vim.cmd("help " .. tag)
end

---@param buf integer
---@param keymaps VimficiencyBufferKeymap[]
function M.set_buffer_keymaps(buf, keymaps)
  for _, m in ipairs(keymaps) do
    assert(type(m.desc) == "string" and m.desc ~= "",
      "vimficiency buffer keymaps require a desc")
    vim.keymap.set(m.mode or "n", m.lhs, m.handler, {
      buffer = buf,
      desc = m.desc,
      nowait = m.nowait ~= false,
      silent = m.silent ~= false,
      expr = m.expr,
      remap = m.remap,
    })
  end
end

---@param title string
---@param keymaps VimficiencyBufferKeymap[]
---@param help_tag string?
function M.show_keymap_help(title, keymaps, help_tag)
  local lines = { title, "" }
  local width = vim.fn.strdisplaywidth(title)

  for _, m in ipairs(keymaps) do
    local line = string.format("  %-8s %s", m.lhs, m.desc)
    lines[#lines + 1] = line
    width = math.max(width, vim.fn.strdisplaywidth(line))
  end

  if help_tag then
    lines[#lines + 1] = ""
    local footer = "Press g? for full docs."
    lines[#lines + 1] = footer
    width = math.max(width, vim.fn.strdisplaywidth(footer))
  end

  local buf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false
  vim.bo[buf].filetype = "vimficiency"

  local ui = v.nvim_list_uis()[1]
  local win_width = math.min(width + 4, math.max(24, ui.width - 4))
  local win_height = math.min(#lines, math.max(4, ui.height - 4))
  local row = math.max(0, math.floor((ui.height - win_height) / 2) - 1)
  local col = math.max(0, math.floor((ui.width - win_width) / 2))

  local win = v.nvim_open_win(buf, true, {
    relative = "editor",
    row = row,
    col = col,
    width = win_width,
    height = win_height,
    style = "minimal",
    border = "rounded",
    title = " Vimficiency Keys ",
    title_pos = "center",
    noautocmd = true,
  })

  vim.wo[win].wrap = false
  vim.wo[win].cursorline = false
  vim.wo[win].number = false
  vim.wo[win].relativenumber = false
  vim.wo[win].signcolumn = "no"

  local function close_popup()
    if v.nvim_win_is_valid(win) then
      v.nvim_win_close(win, true)
    end
  end

  M.set_buffer_keymaps(buf, {
    { lhs = "q", handler = close_popup, desc = "Close keymap summary", nowait = true },
    { lhs = "<Esc>", handler = close_popup, desc = "Close keymap summary", nowait = true },
    { lhs = "<CR>", handler = close_popup, desc = "Close keymap summary", nowait = true },
  })
end

---@param keymaps VimficiencyBufferKeymap[]
---@param title string
---@param help_tag string
---@return VimficiencyBufferKeymap[]
function M.with_help_keymaps(keymaps, title, help_tag)
  local merged = vim.deepcopy(keymaps)
  local function show_help()
    M.show_keymap_help(title, merged, help_tag)
  end
  merged[#merged + 1] = {
    lhs = "?",
    handler = show_help,
    desc = "Show keymap summary",
    nowait = true,
  }
  merged[#merged + 1] = {
    lhs = "g?",
    handler = function() M.open_help(help_tag) end,
    desc = "Open full help",
    nowait = true,
  }
  return merged
end

---@param title string
---@param text? string
---@param opts? { help_tag?: string, help_title?: string }
---@return integer buf
---@return integer win
function M.show_output(title, text, opts)
  opts = opts or {}
  local lines = { title }
  if text and text ~= "" then
    lines[#lines + 1] = ""
    vim.list_extend(lines, vim.split(text, "\n", { plain = true }))
  end

  vim.cmd("botright new") -- create split + window
  local win = v.nvim_get_current_win()

  local buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(win, buf)

  v.nvim_buf_set_name(buf, ("vimficiency://%s"):format(title:gsub("%s+", "_")))
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)

  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false
  vim.bo[buf].filetype = "vimficiency"

  vim.wo[win].number = false
  vim.wo[win].relativenumber = false
  vim.wo[win].wrap = false
  vim.wo[win].signcolumn = "no"

  local keymaps = {
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close vimficiency view", nowait = true },
  }
  if opts.help_tag then
    keymaps = M.with_help_keymaps(
      keymaps,
      opts.help_title or (title .. " Keys"),
      opts.help_tag)
  end
  M.set_buffer_keymaps(buf, keymaps)

  return buf, win
end

-- Wall-clock milliseconds plus a per-ms sequence number.
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

  -- Clamp if the clock goes backwards.
  if ms < last_ms then
    ms = last_ms
    sec = math.floor(ms / 1000)
  end

  if ms == last_ms then
    if seq < SEQ_MAX then
      seq = seq + 1
    else
      ms, sec = wait_next_ms(last_ms)
      last_ms = ms
      seq = 0
    end
  else
    last_ms = ms
    seq = 0
  end

  local wall = os.date("%Y%m%d-%H%M%S", sec)
    .. string.format("-%03d", ms % 1000)

  return string.format("%s__%s__%d__%04d__b%d", base, wall, ms, seq, buf)
end

--- Return true if `s` looks like a session id produced by `new_id`.
---@param s any
---@return boolean
function M.is_session_id(s)
  return type(s) == "string" and s:find("__", 1, true) ~= nil
end
---@return VimficiencyState
function M.capture_state(buf, win)
  assert(buf and win, "capture_state: buf and win required")
  assert(v.nvim_buf_is_valid(buf), "capture state: invalid buffer")
  assert(v.nvim_win_is_valid(win), "capture state: invalid window")
  assert(v.nvim_win_get_buf(win) == buf, "capture state: buffer not in window")

  local lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  local cursor = v.nvim_win_get_cursor(win)
  local top_row = vim.fn.line('w0')
  local bottom_row = vim.fn.line('w$')
  local window_height = vim.api.nvim_win_get_height(win)
  local scroll_amount = vim.api.nvim_get_option_value('scroll', {win=win})

  return new_state(
    v.nvim_buf_get_name(buf),
    vim.bo[buf].filetype,
    cursor[1] - 1,
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
  return ffi_lib.compute_search_region(
    start_lines,
    end_lines,
    start_row,
    end_row,
    padding
  )
end

return M
