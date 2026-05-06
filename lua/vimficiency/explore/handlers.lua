local ffi_lib        = require("vimficiency.ffi")
local key_tracking   = require("vimficiency.capture.key_tracking")
local keynorm        = require("vimficiency.capture.keynorm")
local header_render  = require("vimficiency.explore.render.header")
local insert_helpers = require("vimficiency.explore.insert_helpers")
local lifecycle      = require("vimficiency.explore.lifecycle")
local list_render    = require("vimficiency.explore.render.list")
local registry       = require("vimficiency.explore.registry")
local state          = require("vimficiency.explore.state")
local autocmds       = require("vimficiency.explore.autocmds")

local M = {}
local v = vim.api

local function clear_key_buffer(view)
  view.on_key_buffer = ""
  view.on_key_events = {}
end

local function refresh_key_buffer(view)
  if view.on_key_events and #view.on_key_events > 0 then
    view.on_key_buffer = key_tracking.build_sequence(view.on_key_events)
  end
end

local function mode_is_insert()
  local mode = v.nvim_get_mode().mode
  local first = mode:sub(1, 1)
  return first == "i" or first == "R"
end

local function lines_equal(a, b)
  if #a ~= #b then return false end
  for i = 1, #a do
    if a[i] ~= b[i] then return false end
  end
  return true
end

local function render_warning(view)
  local continuation = insert_helpers.current_continuation(view)
  header_render.render(view, continuation)
  list_render.render(view, continuation)
end

local function set_background_warning(view, title, lines)
  view.warning = {
    title = title,
    lines = lines,
  }
  render_warning(view)
end

local function notify_plan_deviation(reason)
  vim.notify("vimfy explore: action deviated from the plan; reverted scratch (" ..
    reason .. ")", vim.log.levels.WARN)
end

local function check_background_invariants(view, context)
  if not (view and view.scratch
      and v.nvim_win_is_valid(view.scratch.win)
      and v.nvim_buf_is_valid(view.scratch.buf)) then
    return
  end

  local ok, expected_lines = pcall(ffi_lib.explore_current_lines, view.view_id)
  if not ok then
    set_background_warning(view, "Explore invariant check could not read backend lines", {
      "context=" .. context,
      "error=" .. tostring(expected_lines),
    })
    return
  end

  local live_lines = v.nvim_buf_get_lines(view.scratch.buf, 0, -1, false)
  local live_cursor = v.nvim_win_get_cursor(view.scratch.win)
  local live_row, live_col = live_cursor[1] - 1, live_cursor[2]
  local mode = v.nvim_get_mode().mode
  local first = mode:sub(1, 1)
  local insert_like = first == "i" or first == "R"
  local phase_kind = view.state.phase.kind
  local violations = {}

  if not lines_equal(live_lines, expected_lines) then
    violations[#violations + 1] = "scratch_lines=" .. vim.inspect(live_lines)
    violations[#violations + 1] = "backend_lines=" .. vim.inspect(expected_lines)
  end

  if live_row ~= view.state.cursor.row or live_col ~= view.state.cursor.col then
    violations[#violations + 1] = string.format(
      "scratch_cursor=(%d,%d) backend_cursor=(%d,%d)",
      live_row, live_col, view.state.cursor.row, view.state.cursor.col)
  end

  if insert_like and phase_kind ~= "Insert" then
    violations[#violations + 1] = string.format(
      "mode=%s is insert-like but backend phase=%s", mode, phase_kind)
  elseif phase_kind == "Insert" and not insert_like then
    violations[#violations + 1] = string.format(
      "backend phase=Insert but mode=%s is not insert-like", mode)
  end

  if #violations == 0 then
    view.warning = nil
    return
  end

  table.insert(violations, 1, "context=" .. context)
  set_background_warning(view, "Explore invariant check failed", violations)
end

local function valid_scratch(view)
  return view and view.scratch
    and v.nvim_win_is_valid(view.scratch.win)
    and v.nvim_buf_is_valid(view.scratch.buf)
end

local function finish_rejected_restore(view)
  if not (view and view.restoring) then return end
  local context = view.restoring.context
  if not valid_scratch(view) then return end
  state.restore(view)
  check_background_invariants(view, context)
end

local function restore_rejected_snapshot(view, reason, leave_insert_first)
  local context = "rejected snapshot: " .. reason
  view.restoring = { context = context }

  if not leave_insert_first then
    finish_rejected_restore(view)
    return
  end

  vim.schedule(function()
    if mode_is_insert() then
      pcall(vim.cmd, "stopinsert")
    end
    vim.defer_fn(function()
      if not (view and view.restoring) then return end
      if mode_is_insert() then
        pcall(vim.cmd, "stopinsert")
        return
      end
      finish_rejected_restore(view)
    end, 20)
  end)
end

local function render_insert_phase(view)
  if not view.pending then
    local insert_rec = view.recommendations[1]
    if insert_rec then
      local cursor = v.nvim_win_get_cursor(view.scratch.win)
      view.pending = {
        target = insert_rec.text,
        literal_target = insert_rec.literal_text ~= "" and insert_rec.literal_text or insert_rec.text,
        row = cursor[1] - 1,
        col_start = cursor[2],
      }
    end
  end

  local continuation = insert_helpers.current_continuation(view)
  header_render.render(view, continuation)
  list_render.render(view, continuation)
end

local function accept_live_snapshot(insert_mode)
  local view = registry.current()
  local lines = v.nvim_buf_get_lines(view.scratch.buf, 0, -1, false)
  local pos = v.nvim_win_get_cursor(view.scratch.win)
  local new_row, new_col = pos[1] - 1, pos[2]
  refresh_key_buffer(view)
  local raw_keys = view.on_key_buffer
  local restore_from_insert = insert_mode or mode_is_insert()

  local applied = ffi_lib.explore_accept_snapshot(
    view.view_id, lines, new_row, new_col, raw_keys, insert_mode)
  if applied.status == "Rejected" then
    view.pending = nil
    view.warning = nil
    clear_key_buffer(view)
    notify_plan_deviation(applied.reason)
    restore_rejected_snapshot(view, applied.reason, restore_from_insert)
    return false
  end

  view.warning = nil
  state.fetch(view)
  if view.state.phase.kind == "Insert" then
    render_insert_phase(view)
    return true
  end

  view.pending = nil
  clear_key_buffer(view)
  state.refresh_ui(view)
  check_background_invariants(view, "accepted snapshot")
  return true
end

function M.on_cursor_moved()
  local view = registry.current()
  if view.restoring then return end
  accept_live_snapshot(mode_is_insert())
end

function M.on_insert_enter()
  local view = registry.current()
  if view.restoring then return end
  accept_live_snapshot(true)
end

function M.on_insert_text_changed()
  local view = registry.current()
  if view.state.phase.kind ~= "Insert" then return end
  local continuation = insert_helpers.current_continuation(view)
  header_render.render(view, continuation)
  list_render.render(view, continuation)
end

function M.on_buffer_changed(event)
  local view = registry.current()
  if view.restoring then
    if event and event.event == "InsertLeave" then
      finish_rejected_restore(view)
    end
    return
  end
  if view.state.phase.kind == "Insert" and event and event.event == "TextChanged" then
    return
  end
  accept_live_snapshot(mode_is_insert())
end

---@param view VF.Explore.Active
---@param layout VF.Explore.Layout
---@param win integer
---@return boolean
local function owns_window(view, layout, win)
  if win == view.scratch.win or win == layout.list_win or win == view.header.summary.win then
    return true
  end
  for _, pane in ipairs(view.header.windows) do
    if pane.win == win then return true end
  end
  return false
end

---@param view VF.Explore.Active
---@param layout VF.Explore.Layout
---@return integer
function M.install(view, layout)
  return autocmds.install(layout.scratch_buf, {
    on_cursor_moved        = M.on_cursor_moved,
    on_buffer_changed      = M.on_buffer_changed,
    on_insert_enter        = M.on_insert_enter,
    on_insert_text_changed = M.on_insert_text_changed,
    on_winclosed           = function(args)
      if view.header.rebuilding then return end
      local win = tonumber(args.match)
      if win and owns_window(view, layout, win) then lifecycle.destroy(view) end
    end,
  })
end

---@param key string|nil
---@param typed string|nil
function M.capture_key(key, typed)
  if not (typed and typed ~= "") then return end
  if not registry.current_buffer_is_scratch() then return end
  local view = registry.current()
  view.on_key_events = view.on_key_events or {}
  local sent = key or ""

  -- Resolution events carry the whole LHS; drop already-recorded pending bytes.
  if #typed > 1 and typed ~= sent then
    key_tracking.strip_matching_tail(view.on_key_events, typed)
    refresh_key_buffer(view)
    return
  end

  view.on_key_events[#view.on_key_events + 1] = {
    t = vim.uv.hrtime(),
    mode = v.nvim_get_mode().mode,
    win = v.nvim_get_current_win(),
    buf = v.nvim_get_current_buf(),
    key_sent_raw = sent,
    key_sent = keynorm.normalize(sent),
    key_typed_raw = typed,
    key_typed = keynorm.normalize(typed),
  }
  refresh_key_buffer(view)
end

M.clear_key_buffer = clear_key_buffer

return M
