local v = vim.api
local max = math.max
local min = math.min

local util = require("vimficiency.util")

local M = {}

---@class VF.Replay.Keymap
---@field lhs string
---@field handler fun()
---@field desc string
---@field summary_group string?
---@field summary_desc string?

---@param ctx table
---@return table
function M.new(ctx)
  local actions = {}

  local function current_window_is_replay_window()
    local cur_win = v.nvim_get_current_win()
    for _, entry in ipairs(ctx.state.windows) do
      if entry.win == cur_win then
        return true
      end
    end
    return false
  end

  function actions.step_right()
    ctx.step_forward()
  end

  function actions.step_left()
    if ctx.state.global_step > 0 then
      ctx.seek_to(ctx.state.global_step - 1)
    else
      ctx.refresh()
    end
  end

  ---@param step integer
  function actions.cycle(step)
    local focus_state = ctx.get_focus_state()
    if focus_state then
      local n = #focus_state.saved_entries
      if n < 2 then return end
      local new_idx = ((focus_state.focused_idx - 1 + step) % n + n) % n + 1
      local entry = ctx.state.windows[1]
      local saved = focus_state.saved_entries[new_idx]
      v.nvim_win_set_buf(entry.win, saved.buf)
      entry.buf = saved.buf
      entry.is_user = saved.is_user
      entry.seq_idx = saved.seq_idx
      entry.default_rank = saved.default_rank
      focus_state.focused_idx = new_idx
      ctx.seek_to(ctx.state.global_step)
      return
    end

    if #ctx.state.windows < 2 then return end
    local cur_win = v.nvim_get_current_win()
    for i, e in ipairs(ctx.state.windows) do
      if e.win == cur_win then
        local n = #ctx.state.windows
        local nxt = ctx.state.windows[((i - 1 + step) % n + n) % n + 1]
        if v.nvim_win_is_valid(nxt.win) then
          v.nvim_set_current_win(nxt.win)
        end
        return
      end
    end
  end

  function actions.cycle_next()
    actions.cycle(1)
  end

  function actions.cycle_prev()
    actions.cycle(-1)
  end

  function actions.yank_sequence()
    local cur_win = v.nvim_get_current_win()
    for _, entry in ipairs(ctx.state.windows) do
      if entry.win == cur_win then
        local pool_entry = ctx.pool_entry_for(entry)
        if not pool_entry then return end
        local parts = {}
        for _, tok in ipairs(pool_entry.tokens) do parts[#parts + 1] = tok.text end
        local seq = table.concat(parts, "")
        vim.fn.setreg('"', seq)
        vim.fn.setreg('+', seq)
        local tag = entry.is_user and "you" or tostring(entry.seq_idx)
        vim.notify("vimfy: yanked [" .. tag .. "] " .. seq, vim.log.levels.INFO)
        return
      end
    end
  end

  function actions.debug_dump()
    local out = {}
    local function pr(s) out[#out + 1] = s end

    pr("=== vimfy replay debug ===")
    pr(string.format("global_step = %d (max = %d)",
      ctx.state.global_step, ctx.max_total_steps()))
    pr(string.format("focus_state = %s",
      ctx.get_focus_state() and vim.inspect(ctx.get_focus_state()) or "nil"))
    pr(string.format("current_win = %d", v.nvim_get_current_win()))

    for i, entry in ipairs(ctx.state.windows) do
      local pool_entry = ctx.pool_entry_for(entry) or { tokens = {}, states = {} }
      local states = pool_entry.states or {}
      local tokens = pool_entry.tokens or {}
      local snap_idx = min(ctx.state.global_step + 1, #states)
      local snap = states[snap_idx]
      local initial = states[1]
      local rendered = v.nvim_win_is_valid(entry.win)
          and v.nvim_win_get_cursor(entry.win)
          or { -1, -1 }
      local live_lines = v.nvim_buf_is_valid(entry.buf)
          and v.nvim_buf_get_lines(entry.buf, 0, -1, true)
          or {}
      local active_token = tokens[ctx.state.global_step]

      pr(string.format(
        "window[%d]: win=%d buf=%d is_user=%s seq_idx=%d default_rank=%d #tokens=%d #states=%d",
        i, entry.win, entry.buf, tostring(entry.is_user), entry.seq_idx,
        entry.default_rank or 0, #tokens, #states))
      pr(string.format("  active token = %s (%s)  (global_step = %d)",
        active_token and string.format("%q", active_token.text) or "nil",
        active_token and active_token.kind or "-",
        ctx.state.global_step))
      pr(string.format("  rendered cursor = (%d,%d)", rendered[1], rendered[2]))
      if initial then
        pr(string.format(
          "  snapshot[1]  (initial) = cursor=(%d,%d) mode=%s",
          initial.cursor[1], initial.cursor[2], initial.mode))
        pr(string.format("    initial line[%d] = %s",
          initial.cursor[1], vim.inspect(initial.lines[initial.cursor[1]] or "")))
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

  function actions.toggle_focus()
    if ctx.get_focus_state() then
      ctx.escape()
      return
    end
    local cur_win = v.nvim_get_current_win()
    for idx, entry in ipairs(ctx.state.windows) do
      if entry.win == cur_win then
        ctx.focus(idx)
        return
      end
    end
    vim.notify("vimfy: cursor is not in a replay window", vim.log.levels.WARN)
  end

  ---@type VF.SettingsWidget.Handle?
  local active_settings_handle = nil

  function actions.open_settings()
    if active_settings_handle and active_settings_handle.is_open() then
      active_settings_handle.close()
      return
    end
    if #ctx.state.windows == 0 or not current_window_is_replay_window() then
      vim.notify("vimfy: play settings are only available from a replay window",
        vim.log.levels.WARN)
      return
    end
    active_settings_handle = require("vimficiency.play").open_settings({
      on_close = function()
        active_settings_handle = nil
        ctx.relayout_grid()
      end,
    })
  end

  ---@param step integer
  function actions.adjust_window_count(step)
    local play = require("vimficiency.play")
    local settings = play.get_settings()
    local current = settings.window_count or 2
    local new_count = max(1, min(4, current + step))
    if new_count == current then return end
    play.set_setting("window_count", new_count)
    ctx.relayout_grid()
  end

  function actions.toggle_include_user()
    local play = require("vimficiency.play")
    local settings = play.get_settings()
    play.set_setting("include_user_sequence", not settings.include_user_sequence)
    ctx.relayout_grid()
  end

  ---@param step integer
  function actions.cycle_rank(step)
    if #ctx.state.windows == 0 then return end
    local cur_win = v.nvim_get_current_win()
    local entry
    for _, e in ipairs(ctx.state.windows) do
      if e.win == cur_win then entry = e; break end
    end
    if not entry or entry.is_user then return end

    local n = #ctx.state.pool.suggestions
    if n < 2 then return end
    local new_rank = ((entry.seq_idx - 1 + step) % n + n) % n + 1
    if new_rank == entry.seq_idx then return end
    entry.seq_idx = new_rank

    ctx.update_cursor_highlights()
    ctx.update_status_bar()

    ctx.ensure_states_for_ranks({ new_rank }, function()
      local pool_entry = ctx.pool_entry_for(entry)
      if not pool_entry or not pool_entry.states then return end
      if not v.nvim_win_is_valid(entry.win) then return end
      local states = assert(pool_entry.states)
      ctx.apply_state(entry, states[min(ctx.state.global_step + 1, #states)])
      ctx.refresh()
    end)
  end

  actions.keymaps = util.with_standard_ui_keymaps({
    { lhs = "<Left>",  handler = actions.step_left,  desc = "Step backward",
      summary_group = "step", summary_desc = "Step backward / forward" },
    { lhs = "<Right>", handler = actions.step_right, desc = "Step forward",
      summary_group = "step", summary_desc = "Step backward / forward" },
    { lhs = "<Tab>",   handler = actions.cycle_next, desc = "Cycle pane focus (next)",
      summary_group = "cycle_pane", summary_desc = "Cycle pane focus (Tab / S-Tab)" },
    { lhs = "<S-Tab>", handler = actions.cycle_prev, desc = "Cycle pane focus (prev)",
      summary_group = "cycle_pane", summary_desc = "Cycle pane focus (Tab / S-Tab)" },
    { lhs = "<Up>",    handler = function() actions.cycle_rank(-1) end,
      desc = "Cycle this pane to a better-ranked suggestion",
      summary_group = "cycle_rank", summary_desc = "Cycle pane's suggestion rank (Up/Down)" },
    { lhs = "<Down>",  handler = function() actions.cycle_rank(1) end,
      desc = "Cycle this pane to a worse-ranked suggestion",
      summary_group = "cycle_rank", summary_desc = "Cycle pane's suggestion rank (Up/Down)" },
    { lhs = "+",       handler = function() actions.adjust_window_count(1) end,
      desc = "Add a pane (up to 4)",
      summary_group = "pane_count", summary_desc = "Add / remove a pane (+ / -)" },
    { lhs = "-",       handler = function() actions.adjust_window_count(-1) end,
      desc = "Remove a pane (down to 1)",
      summary_group = "pane_count", summary_desc = "Add / remove a pane (+ / -)" },
    { lhs = "u",       handler = actions.toggle_include_user,
      desc = "Toggle user-sequence pane" },
    { lhs = "<CR>",    handler = actions.toggle_focus, desc = "Focus / unfocus current buffer" },
    { lhs = "<leader>y", handler = actions.yank_sequence, desc = "Yank this window's sequence" },
    { lhs = "D",         handler = actions.debug_dump,    desc = "Dump replay state to :messages" },
    { lhs = "q",         handler = function() ctx.cleanup() end, desc = "Close replay" },
  }, {
    title = "Vimfy Replay Keys",
    docs = true,
    settings = {
      lhs = "gs",
      handler = actions.open_settings,
      desc = "Toggle play settings",
    },
  })

  return actions
end

return M
