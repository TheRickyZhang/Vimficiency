local v = vim.api
local cmd = vim.cmd
local max = math.max
local min = math.min

local actions_mod = require("vimficiency.simulate.actions")
local pool_mod = require("vimficiency.simulate.pool")
local precompute = require("vimficiency.simulate.precompute")
local render = require("vimficiency.simulate.render")
local tokenize = require("vimficiency.simulate.tokenize")
local windows = require("vimficiency.simulate.windows")

local M = {}

---@class VF.Replay.Window
---@field win integer
---@field buf integer
---@field is_user boolean
---@field seq_idx integer
---@field default_rank integer

---@class VF.Replay.Snapshot
---@field lines string[]
---@field cursor [integer, integer]
---@field mode string

---@class VF.Replay.PoolEntry
---@field tokens VF.Sequence.Token[]
---@field cost string?
---@field states VF.Replay.Snapshot[]?

---@class VF.Replay.Pool
---@field user VF.Replay.PoolEntry?
---@field suggestions VF.Replay.PoolEntry[]

---@class VF.Replay.State
---@field global_step integer
---@field windows VF.Replay.Window[]
---@field pool VF.Replay.Pool
---@field saved_win integer?
---@field saved_tab integer?
---@field sim_tab integer?
---@field replay_label string?
---@field start_row integer?
---@field start_col integer?
---@field end_row integer?
---@field end_col integer?
---@field status_win integer?
---@field status_buf integer?
---@field precompute_gen integer
---@field source_lines string[]?
---@field source_row integer?
---@field source_col integer?
---@field source_opts table?

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

local ctx = { state = multi_sim }

local function get_focus_state()
  return focus_state
end

---@param value VF.Replay.FocusState?
local function set_focus_state(value)
  focus_state = value
end

---@param entry VF.Replay.Window
---@return VF.Replay.PoolEntry?
local function pool_entry_for(entry)
  if entry.is_user then return multi_sim.pool.user end
  return multi_sim.pool.suggestions[entry.seq_idx]
end

---@return integer
local function max_total_steps()
  local m = 0
  for _, entry in ipairs(multi_sim.windows) do
    local pool_entry = pool_entry_for(entry)
    if pool_entry and #pool_entry.tokens > m then
      m = #pool_entry.tokens
    end
  end
  return m
end

local function cleanup_multi_sim()
  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  render.destroy_status_bar(ctx)

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

---@param target integer
local function seek_to(target)
  target = max(0, min(target, max_total_steps()))
  multi_sim.global_step = target
  for _, entry in ipairs(multi_sim.windows) do
    local pool_entry = pool_entry_for(entry)
    if pool_entry and pool_entry.states and #pool_entry.states > 0 then
      local states = assert(pool_entry.states)
      render.apply_state(entry, states[min(target + 1, #states)])
    end
  end
  render.refresh(ctx)
end

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

---@return boolean
local function step_forward()
  if multi_sim.global_step >= step_cap() then return false end
  seek_to(multi_sim.global_step + 1)
  return true
end

ctx.get_focus_state = get_focus_state
ctx.set_focus_state = set_focus_state
ctx.pool_entry_for = pool_entry_for
ctx.max_total_steps = max_total_steps
ctx.cleanup = cleanup_multi_sim
ctx.seek_to = seek_to
ctx.step_forward = step_forward
ctx.apply_state = render.apply_state
ctx.refresh = function() render.refresh(ctx) end
ctx.update_cursor_highlights = function() render.update_cursor_highlights(ctx) end
ctx.update_status_bar = function() render.update_status_bar(ctx) end
ctx.create_status_bar = function() render.create_status_bar(ctx) end
ctx.destroy_status_bar = function() render.destroy_status_bar(ctx) end
ctx.focus = function(idx) windows.focus(ctx, idx) end
ctx.escape = function() windows.escape(ctx) end
ctx.relayout_grid = function() return M.relayout_grid() end

local actions = actions_mod.new(ctx)
ctx.actions = actions

---@param ranks VF.Replay.PoolRankKey[]
---@param cb fun()
local function ensure_states_for_ranks(ranks, cb)
  local my_gen = multi_sim.precompute_gen
  local function step(i)
    if multi_sim.precompute_gen ~= my_gen then return end
    if i > #ranks then cb(); return end
    local pool_entry = pool_mod.entry_by_rank(multi_sim.pool, ranks[i])
    if not pool_entry or pool_entry.states then
      step(i + 1)
      return
    end
    precompute.states(
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

ctx.ensure_states_for_ranks = ensure_states_for_ranks

---@class VF.Replay.Opts
---@field label string?
---@field end_row integer?
---@field end_col integer?
---@field initial_window_count integer?

---@class VF.Replay.PoolArg
---@field user { seq: string, cost: string? }?
---@field suggestions { seq: string, cost: string? }[]

---@param lines string[]
---@param row integer
---@param col integer
---@param pool_arg VF.Replay.PoolArg
---@param opts VF.Replay.Opts?
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

  if opts.initial_window_count then
    require("vimficiency.play").set_setting(
      "window_count", max(1, min(4, opts.initial_window_count)))
  end

  multi_sim.pool = pool_mod.build(pool_arg)

  local plan = pool_mod.layout_plan(multi_sim.pool)
  local ranks = pool_mod.ranks_for_plan(plan)

  vim.notify("vimfy: precomputing replay...", vim.log.levels.INFO)

  ensure_states_for_ranks(ranks, function()
    if multi_sim.precompute_gen ~= my_gen then return end
    windows.build_grid(ctx, lines, row, col, plan)
  end)
end

function M.relayout_grid()
  if not multi_sim.source_lines then return false end
  local resume_step = multi_sim.global_step

  windows.teardown_keep_pool(ctx)

  local plan = pool_mod.layout_plan(multi_sim.pool)
  local ranks = pool_mod.ranks_for_plan(plan)

  multi_sim.precompute_gen = multi_sim.precompute_gen + 1
  local my_gen = multi_sim.precompute_gen

  ensure_states_for_ranks(ranks, function()
    if multi_sim.precompute_gen ~= my_gen then return end
    windows.build_grid(ctx, multi_sim.source_lines, multi_sim.source_row or 0,
      multi_sim.source_col or 0, plan)
    if resume_step > 0 then
      seek_to(resume_step)
    end
  end)
  return true
end

M.cleanup_compare = cleanup_multi_sim
M.focus = function(idx) windows.focus(ctx, idx) end
M.escape = function() windows.escape(ctx) end
M.open_settings = actions.open_settings

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
M._debug_toggle_focus = actions.toggle_focus
M._debug_cycle_next = actions.cycle_next
M._debug_cycle_prev = actions.cycle_prev
M._debug_cycle_rank_next = function() actions.cycle_rank(1) end
M._debug_cycle_rank_prev = function() actions.cycle_rank(-1) end
M._debug_tokenize_for_animation = tokenize.for_animation

return M
