local v = vim.api
local cmd = vim.cmd
local max = math.max
local min = math.min

local chunks_util = require("vimficiency.chunks")
local highlights = require("vimficiency.highlights")
local sequence_display = require("vimficiency.sequence_display")
local util = require("vimficiency.util")

local M = {}

local cursor_ns = v.nvim_create_namespace("vimfy_cursor")
local wrap_chunks = chunks_util.wrap

---@param mode string
---@return string
local function mode_hl(mode)
  if mode:sub(1, 1) == "i" then return highlights.REPLAY_CURSOR_INSERT end
  if mode == "v" or mode == "V" or mode == "\22" then
    return highlights.REPLAY_CURSOR_VISUAL
  end
  return highlights.REPLAY_CURSOR
end

---@param mode string
---@return string
local function mode_label(mode)
  if mode:sub(1, 1) == "i" then return "INSERT" end
  if mode == "v" or mode == "V" or mode == "\22" then return "VISUAL" end
  return "NORMAL"
end

---@param ctx table
---@param entry VF.Replay.Window
---@return VF.Replay.Snapshot?
function M.current_snap(ctx, entry)
  local pool_entry = ctx.pool_entry_for(entry)
  if not pool_entry or not pool_entry.states or #pool_entry.states == 0 then
    return nil
  end
  local states = assert(pool_entry.states)
  return states[min(ctx.state.global_step + 1, #states)]
end

---@param entry VF.Replay.Window
---@return string
local function header_label(entry)
  if entry.is_user then return "[you] " end
  return string.format("[%d] ", entry.seq_idx)
end

---@param ctx table
---@param entry VF.Replay.Window
---@return table[]?
local function build_virt_lines(ctx, entry)
  local snap = M.current_snap(ctx, entry)
  if not snap then return nil end

  local pool_entry = ctx.pool_entry_for(entry)
  if not pool_entry then return nil end

  local tokens = pool_entry.tokens
  local local_step = min(ctx.state.global_step, #tokens)
  local width = max(8, v.nvim_win_get_width(entry.win))
  local highlight_index = ctx.state.global_step > 0
      and ctx.state.global_step <= #tokens
      and ctx.state.global_step
      or nil
  local display_lines = sequence_display.chunked_lines_for_tokens(tokens, {
    highlight_index = highlight_index,
    highlight_group = highlights.REPLAY_CURRENT,
  })

  local is_active = entry.win == v.nvim_get_current_win()
  local label_hl = is_active and highlights.REPLAY_ACTIVE or "Comment"

  local info_row = {
    { header_label(entry), label_hl },
    { string.format("Local %d/%d", local_step, #tokens), "Comment" },
  }
  if is_active and not entry.is_user
     and entry.default_rank and entry.seq_idx ~= entry.default_rank then
    info_row[#info_row + 1] = {
      string.format("  rank %d/%d", entry.seq_idx, #ctx.state.pool.suggestions),
      highlights.REPLAY_ACTIVE,
    }
  end

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
  for _, line_chunks in ipairs(display_lines) do
    local wrapped = wrap_chunks(line_chunks, max(1, width - #seq_label))
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

  v.nvim_win_call(entry.win, function()
    local view = vim.fn.winsaveview()
    view.topline = 1
    view.topfill = #virt_lines
    vim.fn.winrestview(view)
  end)
end

---@param ctx table
function M.update_cursor_highlights(ctx)
  local cached = {}
  local max_content = 0
  for i, entry in ipairs(ctx.state.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      cached[i] = build_virt_lines(ctx, entry)
      if cached[i] then
        max_content = max(max_content, #cached[i])
      end
    end
  end
  local target_height = max_content + 1

  for i, entry in ipairs(ctx.state.windows) do
    if v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_clear_namespace(entry.buf, cursor_ns, 0, -1)
      apply_virt_lines(entry, cached[i], target_height)

      local snap = M.current_snap(ctx, entry)
      local hl = snap and mode_hl(snap.mode) or highlights.REPLAY_CURSOR

      local cursor = v.nvim_win_get_cursor(entry.win)
      local row = cursor[1] - 1
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

---@param ctx table
---@return string
function M.statusline(ctx)
  local state = ctx.state
  local label = state.replay_label
  local label_part = (label and label ~= "") and ("  " .. label) or ""
  local pos_part = ""
  if state.start_row and state.end_row then
    pos_part = string.format("  (%d:%d -> %d:%d)",
      state.start_row + 1, state.start_col + 1,
      state.end_row + 1, state.end_col + 1)
  end
  local text = string.format(" vimfy%s  step %d/%d%s ",
    label_part, state.global_step, ctx.max_total_steps(), pos_part)
  return (text:gsub("%%", "%%%%"))
end

---@param ctx table
function M.create_status_bar(ctx)
  cmd("botright 1split")
  local win = v.nvim_get_current_win()
  local buf = v.nvim_create_buf(false, true)
  v.nvim_set_option_value("buftype", "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
  v.nvim_win_set_buf(win, buf)

  util.configure_scratch_window(win, {
    winfixheight = true,
    statusline   = " ",
    winhighlight = "Normal:StatusLine,StatusLine:Normal,StatusLineNC:Normal",
  })

  ctx.state.status_win = win
  ctx.state.status_buf = buf
end

---@param ctx table
function M.update_status_bar(ctx)
  local state = ctx.state
  if not state.status_win or not v.nvim_win_is_valid(state.status_win) then
    return
  end
  v.nvim_buf_set_lines(state.status_buf, 0, -1, true, { M.statusline(ctx) })
end

---@param ctx table
function M.destroy_status_bar(ctx)
  local state = ctx.state
  if state.status_win and v.nvim_win_is_valid(state.status_win) then
    v.nvim_win_close(state.status_win, true)
  end
  if state.status_buf and v.nvim_buf_is_valid(state.status_buf) then
    v.nvim_buf_delete(state.status_buf, { force = true })
  end
  state.status_win = nil
  state.status_buf = nil
end

---@param ctx table
function M.refresh(ctx)
  M.update_cursor_highlights(ctx)
  M.update_status_bar(ctx)
  cmd("redraw")
end

---@param entry VF.Replay.Window
---@param snap VF.Replay.Snapshot
function M.apply_state(entry, snap)
  if not (v.nvim_win_is_valid(entry.win) and v.nvim_buf_is_valid(entry.buf)) then
    return
  end
  v.nvim_buf_set_lines(entry.buf, 0, -1, true, snap.lines)
  local line_count = v.nvim_buf_line_count(entry.buf)
  local row = max(1, min(snap.cursor[1], line_count))
  local line = v.nvim_buf_get_lines(entry.buf, row - 1, row, false)[1] or ""
  local col = max(0, min(snap.cursor[2], #line))
  v.nvim_win_set_cursor(entry.win, { row, col })
end

return M
