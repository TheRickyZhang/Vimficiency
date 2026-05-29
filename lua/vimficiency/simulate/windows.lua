local v = vim.api
local cmd = vim.cmd
local max = math.max
local min = math.min

local util = require("vimficiency.util")

local M = {}

---@param win integer
function M.decorate(win)
  util.configure_scratch_window(win, {
    cursorline = true,
    cursorcolumn = true,
  })
end

---@param ctx table
---@param lines string[]
---@param label string
---@return integer
function M.create_buffer(ctx, lines, label)
  local buf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_name(buf, "vimfy-sim-" .. label:gsub("%s+", "_"):sub(1, 20))
  v.nvim_set_option_value("buftype", "nofile", { buf = buf })
  v.nvim_set_option_value("bufhidden", "wipe", { buf = buf })
  v.nvim_set_option_value("swapfile", false, { buf = buf })
  v.nvim_set_option_value("modifiable", true, { buf = buf })
  v.nvim_buf_set_lines(buf, 0, -1, true, lines)

  util.set_buffer_keymaps(buf, ctx.actions.keymaps)

  v.nvim_create_autocmd("WinEnter", {
    buffer = buf,
    callback = function() ctx.update_cursor_highlights() end,
    desc = "vimfy: refresh focus indicator on window enter",
  })

  return buf
end

---@param win integer
---@param buf integer
---@param lines string[]
---@param row integer
---@param col integer
function M.setup_window(win, buf, lines, row, col)
  v.nvim_win_set_buf(win, buf)

  local line_count = #lines
  local safe_row = max(1, min(row + 1, line_count))
  local line_len = #(lines[safe_row] or "")
  local safe_col = max(0, min(col, max(0, line_len - 1)))
  v.nvim_win_set_cursor(win, { safe_row, safe_col })

  M.decorate(win)
end

---@param ctx table
---@param lines string[]
---@param row integer
---@param col integer
---@param plan VF.Replay.LayoutSlot[]
function M.build_grid(ctx, lines, row, col, plan)
  cmd("tabnew")
  ctx.state.sim_tab = v.nvim_get_current_tabpage()
  local tabnew_buf = v.nvim_get_current_buf()

  for i, slot in ipairs(plan) do
    local label = slot.is_user and "you" or tostring(slot.default_rank)
    local buf = M.create_buffer(ctx, lines, label)

    local win
    if i == 1 then
      win = v.nvim_get_current_win()
    else
      cmd("rightbelow vsplit")
      win = v.nvim_get_current_win()
    end

    M.setup_window(win, buf, lines, row, col)
    table.insert(ctx.state.windows, {
      win = win,
      buf = buf,
      is_user = slot.is_user,
      seq_idx = slot.is_user and 0 or slot.default_rank,
      default_rank = slot.default_rank,
    })
  end

  if v.nvim_buf_is_valid(tabnew_buf) then
    v.nvim_buf_delete(tabnew_buf, { force = true })
  end

  cmd("wincmd =")

  if #ctx.state.windows > 0 and v.nvim_win_is_valid(ctx.state.windows[1].win) then
    v.nvim_set_current_win(ctx.state.windows[1].win)
  end

  local primary = v.nvim_get_current_win()
  ctx.create_status_bar()
  if v.nvim_win_is_valid(primary) then
    v.nvim_set_current_win(primary)
  end

  for _, entry in ipairs(ctx.state.windows) do
    local pool_entry = ctx.pool_entry_for(entry)
    if pool_entry and pool_entry.states and #pool_entry.states > 0 then
      ctx.apply_state(entry, pool_entry.states[1])
    end
  end
  ctx.refresh()
end

---@param ctx table
---@param idx integer
function M.focus(ctx, idx)
  if ctx.get_focus_state() then
    vim.notify("vimfy: already focused; use :Vimfy escape first", vim.log.levels.WARN)
    return
  end
  if #ctx.state.windows == 0 then
    vim.notify("vimfy: no replay in progress", vim.log.levels.WARN)
    return
  end
  if not idx or idx < 1 or idx > #ctx.state.windows then
    vim.notify("vimfy: focus index must be 1.." .. #ctx.state.windows,
      vim.log.levels.ERROR)
    return
  end

  local saved_entries = {}
  for i, entry in ipairs(ctx.state.windows) do
    saved_entries[i] = {
      buf = entry.buf,
      is_user = entry.is_user,
      seq_idx = entry.seq_idx,
      default_rank = entry.default_rank,
    }
    v.nvim_set_option_value("bufhidden", "hide", { buf = entry.buf })
  end

  local focused = ctx.state.windows[idx]
  local focus_win = focused.win
  v.nvim_set_current_win(focus_win)
  cmd("only")

  ctx.state.windows = { {
    win = focus_win,
    buf = focused.buf,
    is_user = focused.is_user,
    seq_idx = focused.seq_idx,
    default_rank = focused.default_rank,
  } }
  ctx.set_focus_state({ saved_entries = saved_entries, focused_idx = idx })
  ctx.refresh()
end

---@param ctx table
function M.escape(ctx)
  local focus_state = ctx.get_focus_state()
  if not focus_state then
    vim.notify("vimfy: not currently focused", vim.log.levels.INFO)
    return
  end
  local saved_entries = focus_state.saved_entries
  local focused_idx = focus_state.focused_idx

  local current_win = v.nvim_get_current_win()
  local first = saved_entries[1]
  v.nvim_win_set_buf(current_win, first.buf)
  M.decorate(current_win)

  local new_windows = { {
    win = current_win,
    buf = first.buf,
    is_user = first.is_user,
    seq_idx = first.seq_idx,
    default_rank = first.default_rank,
  } }
  for i = 2, #saved_entries do
    cmd("rightbelow vsplit")
    local new_win = v.nvim_get_current_win()
    local saved = saved_entries[i]
    v.nvim_win_set_buf(new_win, saved.buf)
    M.decorate(new_win)
    new_windows[i] = {
      win = new_win,
      buf = saved.buf,
      is_user = saved.is_user,
      seq_idx = saved.seq_idx,
      default_rank = saved.default_rank,
    }
  end

  for _, saved in ipairs(saved_entries) do
    if v.nvim_buf_is_valid(saved.buf) then
      v.nvim_set_option_value("bufhidden", "wipe", { buf = saved.buf })
    end
  end

  cmd("wincmd =")
  ctx.state.windows = new_windows

  -- focus()'s `cmd("only")` closed the status bar; recreate it (as build_grid
  -- does) so the step/label bar returns after un-focusing.
  local primary = new_windows[focused_idx].win
  v.nvim_set_current_win(primary)
  ctx.create_status_bar()
  if v.nvim_win_is_valid(primary) then
    v.nvim_set_current_win(primary)
  end

  ctx.set_focus_state(nil)
  ctx.seek_to(ctx.state.global_step)
end

---@param ctx table
function M.teardown_keep_pool(ctx)
  ctx.state.precompute_gen = ctx.state.precompute_gen + 1
  ctx.destroy_status_bar()

  local focus_state = ctx.get_focus_state()
  if focus_state then
    for _, saved in ipairs(focus_state.saved_entries) do
      if v.nvim_buf_is_valid(saved.buf) then
        v.nvim_buf_delete(saved.buf, { force = true })
      end
    end
    ctx.set_focus_state(nil)
  end

  for _, entry in ipairs(ctx.state.windows) do
    if v.nvim_buf_is_valid(entry.buf) then
      v.nvim_buf_delete(entry.buf, { force = true })
    end
  end
  ctx.state.windows = {}

  if ctx.state.sim_tab and v.nvim_tabpage_is_valid(ctx.state.sim_tab) then
    if ctx.state.saved_tab and v.nvim_tabpage_is_valid(ctx.state.saved_tab) then
      v.nvim_set_current_tabpage(ctx.state.saved_tab)
    end
    local sim_tab_nr = v.nvim_tabpage_get_number(ctx.state.sim_tab)
    pcall(function() cmd("tabclose " .. sim_tab_nr) end)
  end
  ctx.state.sim_tab = nil
end

return M
