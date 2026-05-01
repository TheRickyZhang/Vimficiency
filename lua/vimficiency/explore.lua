local config           = require("vimficiency.config")
local ffi_lib          = require("vimficiency.ffi")
local util             = require("vimficiency.util")
local keynorm          = require("vimficiency.capture.keynorm")
local highlights       = require("vimficiency.explore.highlights")
local tags_render      = require("vimficiency.explore.render.tags")
local header_render    = require("vimficiency.explore.render.header")
local list_render      = require("vimficiency.explore.render.list")
local keymaps          = require("vimficiency.explore.keymaps")
local settings         = require("vimficiency.settings_modal")
local settings_profile = require("vimficiency.settings_profile")

-- Forward-declared locals. Hoisted to the top of the file so every
-- closure below closes over the right upvalue, regardless of the order
-- definitions appear in.
---@type table<integer, VF.Explore.Active>  # keyed by tabpage id
local active_by_tab = {}
local open_settings_modal

-- Layered settings store. On first access we build the layer stack:
--
--   hardcoded defaults  (in config.lua's `fields.explore`)
--        ↓ overlaid by
--   user's setup declarations  (already merged into `config.explore`
--        by `config.apply`)
--        ↓ overlaid by
--   sidecar file (`~/.local/share/nvim/vimficiency/explore_settings.json`)
--
-- Every runtime change (`gs` modal) mutates this table AND
-- writes to the sidecar so the next nvim run starts with the user's
-- latest preferences. To reset, delete the sidecar or use the settings
-- modal's reset action.
---@type table|nil
local current_settings

---Lazy-init the layered settings store. `vim.deepcopy(config.explore)`
---so mutations don't leak into the config module's shared table.
---@return table
local function settings_store()
  if current_settings == nil then
    current_settings = vim.deepcopy(config.explore or {})
    local saved = settings_profile.load("explore")
    for k, v in pairs(saved) do
      -- Only overlay keys we know about; ignore stale fields from
      -- old schema versions, typos, etc.
      if current_settings[k] ~= nil or config.explore[k] == nil then
        current_settings[k] = v
      end
    end
  end
  return current_settings
end

local M = {}

local v = vim.api
-- Only `on_key_ns` is owned by init — the header/list/tags renderers manage
-- their own namespaces via `nvim_create_namespace(name)`, which is
-- idempotent by name so shared-with-renderer namespaces resolve to the
-- same ID without cross-module wiring.
local on_key_ns = v.nvim_create_namespace("vimfy_explore_on_key")

local BUFFER_OPTIONS = {
  "shiftwidth",
  "tabstop",
  "softtabstop",
  "expandtab",
  "iskeyword",
  "matchpairs",
  "virtualedit",
  "filetype",
}

local WINDOW_OPTIONS = {
  "number",
  "relativenumber",
  "cursorline",
  "wrap",
}

-- Display modes for how recommendation tags render in the scratch buffer.
-- Cycled via `gm` / `gM` in normal mode, or set explicitly via
-- `:Vimfy explore_mode <mode>`. Arbitrary motion keys still flow to the
-- session in all modes — only the visual overlay changes.
--
--   off        side list only.
--   highlight  rank-colored landing cells, no inline labels.
--   inplace    landing cells highlighted AND overwritten with the first
--              char of the suggested command.
--   above      landing cells highlighted, command labels on a virt_line above.
--   below      landing cells highlighted, command labels on a virt_line below.
local DISPLAY_MODES = { "off", "highlight", "inplace", "above", "below" }

-- Number of recommendations surfaced in the side list + tag overlay.
-- Per-view (`view.recommendation_count`), seeded from
-- `config.explore.recommendation_count` on first view open. Mutated
-- by the settings modal; stays in-memory across open/close within one
-- nvim run.
local RECOMMENDATION_COUNT_MIN = 1
local RECOMMENDATION_COUNT_MAX = 10

---@class VF.Explore.Window
---@field buf integer
---@field win integer

---@class VF.Explore.Header
---@field summary VF.Explore.Window
---@field windows VF.Explore.Window[]
---@field rebuilding boolean

---@class VF.Explore.Scratch
---@field buf integer
---@field win integer
---@field tab integer   # the tab we opened for the view; used on teardown

---@class VF.Explore.Pending
---@field target string       # the planned typed text we match against
---@field row integer         # buffer row (0-indexed) where insert started
---@field col_start integer   # buffer col (0-indexed bytes) where insert started

---@class VF.Explore.Active
---@field label string                                 # caller-supplied, shown in the header
---@field result VF.Session.Result                         # the captured session we're exploring
---@field view_id integer                              # FFI handle
---@field header VF.Explore.Header              # fixed panes above the scratch editor
---@field scratch VF.Explore.Scratch            # scrollable editor pane in the right column
---@field list_buf integer                             # recommendation list buffer (left pane)
---@field state VF.Explore.State                # view-reported phase + cursor + seq
---@field recommendations VF.Explore.Recommendation[]
---@field on_key_buffer string                         # raw keys captured since last reconcile
---@field pending VF.Explore.Pending|nil        # insertion-origin snapshot while Insert
---@field display_mode string                          # "off" | "highlight" | "inplace" | "above" | "below"
---@field recommendation_count integer|nil             # settings override, or nil → config default
---@field allow_multiple_movements_per_position boolean  # settings toggle; false → movement recs dedup by landing
---@field allow_multiple_edits_per_position boolean    # settings toggle; false → edit recs dedup by landing
---@field show_user_typed boolean                      # include user's original typed sequence in the header
---@field show_result_count integer                    # how many captured `optimal_results` to include (0 → none)
---@field header_handlers table<string, function>      # keymaps attached to every header pane
---@field winclosed_autocmd integer|nil                # autocmd id for the view's WinClosed watcher

---@return VF.Explore.Active|nil
local function current_view()
  return active_by_tab[v.nvim_get_current_tabpage()]
end

---@return VF.Explore.Active
local function assert_current_view()
  local a = current_view()
  assert(a, "vimfy explore view not active in current tab")
  return a
end

---Update the live view + module store + sidecar file in one shot.
---Auto-save means the user doesn't think about persistence — it just
---happens on every toggle.
---@param a VF.Explore.Active
---@param key string
---@param value any
local function update_setting(a, key, value)
  a[key] = value
  local store = settings_store()
  store[key] = value
  settings_profile.save("explore", store)
end

local function copy_buffer_options(src_buf, scratch_buf)
  for _, opt in ipairs(BUFFER_OPTIONS) do
    local ok, value = pcall(v.nvim_get_option_value, opt, { buf = src_buf })
    if ok then
      pcall(v.nvim_set_option_value, opt, value, { buf = scratch_buf })
    end
  end
end

local function copy_window_options(src_win, scratch_win)
  for _, opt in ipairs(WINDOW_OPTIONS) do
    local ok, value = pcall(v.nvim_get_option_value, opt, { win = src_win })
    if ok then
      pcall(v.nvim_set_option_value, opt, value, { win = scratch_win })
    end
  end
end

---@param result VF.Session.Result
---@return boolean
---@return string|nil
local function ensure_explore_metadata(result)
  if type(result.goal_lines) ~= "table" or #result.goal_lines == 0 then
    return false, "result is missing goal_lines; re-run the session with a newer vimfy build"
  end
  if type(result.has_lines_above) ~= "boolean" or type(result.has_lines_below) ~= "boolean" then
    return false, "result is missing explore boundary metadata; re-run the session with a newer vimfy build"
  end
  return true, nil
end

---@param lines string[]
---@param goal_lines string[]
---@return integer
local function compute_boundary_last_col(lines, goal_lines)
  local current_last = #(lines[#lines] or "")
  local goal_last = #(goal_lines[#goal_lines] or "")
  return math.max(0, math.max(current_last, goal_last) - 1)
end

local function clear_on_key_buffer()
  local a = current_view()
  if a then a.on_key_buffer = "" end
end

local function sync_cursor_to_state()
  local a = assert_current_view()
  v.nvim_win_set_cursor(a.scratch.win, { a.state.cursor.row + 1, a.state.cursor.col })
end

local function refresh_state_and_recommendations()
  local a = assert_current_view()
  a.state = ffi_lib.explore_state(a.view_id)
  a.recommendations = ffi_lib.explore_recommendations(a.view_id,
    a.recommendation_count,
    a.allow_multiple_movements_per_position,
    a.allow_multiple_edits_per_position)
end

---Compute the suffix of the planned typed text still to be typed. Diffs the
---live scratch buffer against the recorded insertion origin so the header
---and recommendation list shrink as the user types matching chars.
---@return string
local function current_remaining(a)
  local pending = a.pending
  local target = (pending and pending.target) or ""
  if not pending then return target end
  local cursor = v.nvim_win_get_cursor(a.scratch.win)
  local cur_row, cur_col = cursor[1] - 1, cursor[2]
  if cur_row ~= pending.row or cur_col < pending.col_start then return target end
  local line = v.nvim_buf_get_lines(a.scratch.buf, pending.row, pending.row + 1, false)[1] or ""
  local typed_so_far = line:sub(pending.col_start + 1, cur_col)
  if target:sub(1, #typed_so_far) == typed_so_far then
    return target:sub(#typed_so_far + 1)
  end
  return target
end

-- For convenience, add a rank field to each recommendation so tag rendering
-- can find them after sorting.
local function attach_ranks()
  local a = assert_current_view()
  for i, rec in ipairs(a.recommendations) do
    rec.rank = i
  end
end

local function refresh_ui()
  refresh_state_and_recommendations()
  attach_ranks()
  sync_cursor_to_state()
  local a = assert_current_view()
  local remaining = current_remaining(a)
  header_render.render(a, remaining)
  list_render.render(a, remaining)
  tags_render.render(a)
end

---Rewrite the scratch buffer to match the view's current lines. The buffer
---stays modifiable throughout — natural edit commands must be able to fire
---between sync calls.
local function sync_buffer_from_session()
  local a = assert_current_view()
  local lines = ffi_lib.explore_current_lines(a.view_id)
  v.nvim_buf_set_lines(a.scratch.buf, 0, -1, false, lines)
  vim.bo[a.scratch.buf].modified = false
end

local function undo()
  clear_on_key_buffer()
  local a = assert_current_view()
  local result = ffi_lib.explore_undo(a.view_id)
  if result.status == "Rejected" then
    vim.notify("vimfy explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  sync_buffer_from_session()
  refresh_ui()
  return true
end

local function redo()
  clear_on_key_buffer()
  local a = assert_current_view()
  local result = ffi_lib.explore_redo(a.view_id)
  if result.status == "Rejected" then
    vim.notify("vimfy explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  sync_buffer_from_session()
  refresh_ui()
  return true
end

local function on_cursor_moved()
  local a = assert_current_view()
  -- Loop-avoidance: if nvim cursor already matches view cursor, we just set it
  -- ourselves (e.g. after apply_movement / undo / redo) — nothing to forward.
  local pos = v.nvim_win_get_cursor(a.scratch.win)
  local new_row, new_col = pos[1] - 1, pos[2]
  if new_row == a.state.cursor.row and new_col == a.state.cursor.col then
    return
  end

  local raw_keys = a.on_key_buffer or ""
  a.on_key_buffer = ""
  local applied = ffi_lib.explore_accept_cursor_move(
    a.view_id, new_row, new_col, raw_keys)
  if applied.status == "Rejected" then
    -- View refused the external move — snap cursor back to the view's state.
    sync_cursor_to_state()
    return
  end
  refresh_ui()
end

---InsertEnter handler — transition the view into Insert when we
---recognize which edit atom the user just triggered. The atom is identified
---by tail-matching the raw-key buffer against known edit recommendations;
---the matched rec's `typed_text` becomes the target the header displays
---to the user. On no match, we do nothing — InsertLeave's
---`accept_insert_exit` fallback still validates the final buffer.
local function on_insert_enter()
  local a = assert_current_view()
  -- Reentrancy: nested <C-o>-style inserts shouldn't re-call begin_insert.
  if a.state.phase.kind ~= "Navigate" and a.state.phase.kind ~= "Transform" then return end

  local keys = a.on_key_buffer or ""
  if keys == "" then return end

  -- Longest-tail match against edit-kind recommendations so stray normal-mode
  -- keystrokes (a dead-end `l` at EOL that didn't fire CursorMoved, etc.)
  -- don't block recognition of the real atom.
  local matched
  for _, rec in ipairs(a.recommendations) do
    if rec.kind == "edit" and rec.text ~= "" and #keys >= #rec.text then
      if keys:sub(#keys - #rec.text + 1) == rec.text then
        if not matched or #rec.text > #matched.text then
          matched = rec
        end
      end
    end
  end
  if not matched then return end

  -- Deliberately DO NOT clear on_key_buffer here — so the full command
  -- (`s` + typed content + `<Esc>`) survives to acceptedSeq when
  -- InsertLeave calls accept_insert_exit.
  local applied = ffi_lib.explore_begin_insert(a.view_id)
  if applied.status == "Rejected" then return end

  -- Snapshot the insertion origin so the UI can shrink `remaining` live
  -- as TextChangedI fires. row/col are from Vim's current cursor (post-atom,
  -- pre-typing) — i.e. where the next typed char will land.
  local cursor = v.nvim_win_get_cursor(a.scratch.win)
  a.pending = {
    target = matched.typed_text or "",
    row = cursor[1] - 1,
    col_start = cursor[2],
  }

  refresh_state_and_recommendations()
  attach_ranks()
  local remaining = current_remaining(a)
  header_render.render(a, remaining)
  list_render.render(a, remaining)
end

---Live-refresh the header + recommendation list as the user types in
---insert mode. Cheap: no FFI calls, just re-reads the buffer diff.
local function on_insert_text_changed()
  local a = assert_current_view()
  if a.state.phase.kind ~= "Insert" then return end
  local remaining = current_remaining(a)
  header_render.render(a, remaining)
  list_render.render(a, remaining)
end

---Strict-revert buffer-state handler. Called on TextChanged (normal mode) and
---InsertLeave. Branches on phase: in Insert the buffer is checked
---against the planned post-edit fencepost via acceptInsertExit; in
---Navigate/Transform the legacy acceptBufferState path still handles normal-mode
---edits (r, x, dd, …). Rejections revert the scratch to the view's
---last-known lines + cursor.
local function on_buffer_changed()
  local a = assert_current_view()
  local new_lines = v.nvim_buf_get_lines(a.scratch.buf, 0, -1, false)
  local pos = v.nvim_win_get_cursor(a.scratch.win)
  local new_row, new_col = pos[1] - 1, pos[2]

  local session_lines = ffi_lib.explore_current_lines(a.view_id)
  local buffer_matches_session = (#new_lines == #session_lines)
  if buffer_matches_session then
    for i = 1, #new_lines do
      if new_lines[i] ~= session_lines[i] then
        buffer_matches_session = false
        break
      end
    end
  end

  if a.state.phase.kind == "Insert" then
    -- Clear the insertion-origin snapshot — we're leaving Insert on
    -- either branch below, so the TextChangedI live-remaining computation
    -- should stop until the next InsertEnter sets it again.
    a.pending = nil

    -- Abandoned insert: user exited without net change. Roll phase back
    -- without polluting redo so we're cleanly in the previous phase again.
    if buffer_matches_session then
      a.on_key_buffer = ""
      ffi_lib.explore_cancel_insert(a.view_id)
      refresh_ui()
      return
    end
    local raw_keys = a.on_key_buffer or ""
    a.on_key_buffer = ""
    local applied = ffi_lib.explore_accept_insert_exit(
      a.view_id, new_lines, new_row, new_col, raw_keys)
    if applied.status == "Applied" then
      refresh_ui()
      return
    end
    -- Reject: cancel the pending phase, revert buffer to pre-edit fencepost.
    vim.notify("vimfy explore: " .. applied.reason, vim.log.levels.WARN)
    ffi_lib.explore_cancel_insert(a.view_id)
    sync_buffer_from_session()
    refresh_ui()
    return
  end

  -- Navigate / Transform — existing strict path. (Insert is handled above;
  -- "Completed" is no longer a phase variant — completion is the
  -- `is_completed` boolean derived from Navigate(total_edits) at goal.)
  if buffer_matches_session then return end

  local raw_keys = a.on_key_buffer or ""
  a.on_key_buffer = ""

  local applied = ffi_lib.explore_accept_buffer_state(
    a.view_id, new_lines, new_row, new_col, raw_keys)

  if applied.status == "Rejected" then
    -- Strict (b): revert the scratch to the view's last known state + cursor.
    vim.notify("vimfy explore: " .. applied.reason, vim.log.levels.WARN)
    sync_buffer_from_session()
    sync_cursor_to_state()
    return
  end
  refresh_ui()
end

---Tear down a specific view. Destroys the C++ FFI view, drops it from the
---tab map, deletes any module-scoped autocmds owned by the view, and
---schedules the scratch tab to close. Safe to call with a view that's
---already been removed from the map.
---@param a VF.Explore.Active|nil
local function destroy_view(a)
  if not a then return end
  local tab = a.scratch.tab
  if a.winclosed_autocmd then
    pcall(v.nvim_del_autocmd, a.winclosed_autocmd)
    a.winclosed_autocmd = nil
  end
  ffi_lib.explore_destroy(a.view_id)
  active_by_tab[tab] = nil
  vim.schedule(function()
    if tab and v.nvim_tabpage_is_valid(tab) then
      pcall(function()
        v.nvim_set_current_tabpage(tab)
        vim.cmd("tabclose")
      end)
    end
  end)
end

-- Module-scoped on_key handler. Registered once at load time; dispatches to
-- whatever view owns the current tab (if any). Registering per-open would
-- collide across concurrent views since `on_key_ns` is a single namespace.
vim.on_key(function(_, typed)
  if not (typed and typed ~= "") then return end
  local a = current_view()
  if not a then return end
  a.on_key_buffer = a.on_key_buffer .. keynorm.normalize(typed)
end, on_key_ns)

function M.open(label, result, opts)
  assert(type(label) == "string" and label ~= "", "explore.open: label must be non-empty")
  assert(type(result) == "table", "explore.open: result must be a VF.Session.Result")

  -- Re-blend against the current Normal group in case the colorscheme changed.
  highlights.refresh()

  local ok, err = ensure_explore_metadata(result)
  if not ok then
    vim.notify("vimfy explore failed: " .. err, vim.log.levels.ERROR)
    return false
  end

  opts = opts or {}

  local source_buf = v.nvim_get_current_buf()
  local source_win = v.nvim_get_current_win()
  local source_tab = v.nvim_get_current_tabpage()

  local initial_lines = result.lines or {}
  local start_row = result.start_row or 0
  local start_col = result.start_col or 0
  local window_height = v.nvim_win_get_height(source_win)
  local scroll_amount = v.nvim_get_option_value("scroll", { win = source_win })
  local boundary_last_col = compute_boundary_last_col(initial_lines, result.goal_lines)

  local view_id = ffi_lib.explore_start(
    initial_lines, start_row, start_col,
    result.goal_lines, result.end_row or 0, result.end_col or 0,
    0, boundary_last_col,
    result.has_lines_above, result.has_lines_below,
    window_height, scroll_amount,
    result.user_seq or "")

  vim.cmd("tabnew")
  local scratch_tab = v.nvim_get_current_tabpage()
  local scratch_win = v.nvim_get_current_win()
  local tabnew_buf = v.nvim_get_current_buf()
  local scratch_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(scratch_win, scratch_buf)
  if v.nvim_buf_is_valid(tabnew_buf) then
    pcall(v.nvim_buf_delete, tabnew_buf, { force = true })
  end

  v.nvim_buf_set_name(scratch_buf, "vimficiency://explore/" .. label)
  v.nvim_buf_set_lines(scratch_buf, 0, -1, false, initial_lines)

  vim.bo[scratch_buf].buftype = "nofile"
  vim.bo[scratch_buf].bufhidden = "wipe"
  vim.bo[scratch_buf].swapfile = false
  vim.bo[scratch_buf].undofile = false
  -- undolevels = -1 disables Vim's native undo so the view is authoritative
  -- about state changes, but the buffer itself stays modifiable so natural
  -- edit commands (r{char}, x, s, c{motion}, etc.) flow through unimpeded.
  vim.bo[scratch_buf].undolevels = -1
  vim.bo[scratch_buf].modifiable = true
  vim.bo[scratch_buf].modified = false

  copy_buffer_options(source_buf, scratch_buf)
  copy_window_options(source_win, scratch_win)

  -- List panel on the LEFT.
  vim.cmd("topleft vsplit")
  local list_win = v.nvim_get_current_win()
  local list_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(list_win, list_buf)
  v.nvim_win_set_width(list_win, 44)
  v.nvim_buf_set_name(list_buf, "vimficiency://explore/" .. label .. "/recommendations")
  vim.bo[list_buf].buftype = "nofile"
  vim.bo[list_buf].bufhidden = "wipe"
  vim.bo[list_buf].swapfile = false
  vim.bo[list_buf].modifiable = false
  vim.bo[list_buf].filetype = "vimficiency"
  util.configure_scratch_window(list_win)
  local wins_before_header = v.nvim_tabpage_list_wins(scratch_tab)
  v.nvim_set_current_win(scratch_win)
  vim.cmd("aboveleft split")
  local columns_win
  for _, win in ipairs(v.nvim_tabpage_list_wins(scratch_tab)) do
    local is_existing = false
    for _, prev in ipairs(wins_before_header) do
      if win == prev then
        is_existing = true
        break
      end
    end
    if not is_existing then
      columns_win = win
      break
    end
  end
  assert(columns_win, "vimfy explore: failed to create header row")
  local columns_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(columns_win, columns_buf)
  local wins_before_summary = v.nvim_tabpage_list_wins(scratch_tab)
  v.nvim_set_current_win(columns_win)
  vim.cmd("aboveleft split")
  local summary_win
  for _, win in ipairs(v.nvim_tabpage_list_wins(scratch_tab)) do
    local is_existing = false
    for _, prev in ipairs(wins_before_summary) do
      if win == prev then
        is_existing = true
        break
      end
    end
    if not is_existing then
      summary_win = win
      break
    end
  end
  assert(summary_win, "vimfy explore: failed to create summary header")
  local summary_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(summary_win, summary_buf)
  v.nvim_set_current_win(scratch_win)

  local view = {
    -- identity
    label = label,
    result = result,
    view_id = view_id,

    -- the three-pane layout we drive
    header = {
      summary = { buf = summary_buf, win = summary_win },
      windows = { { buf = columns_buf, win = columns_win } },
      rebuilding = false,
    },
    scratch = { buf = scratch_buf, win = scratch_win, tab = scratch_tab },
    list_buf = list_buf,

    -- session-reported state + derived ranking
    state = {
      phase = { kind = "Navigate" },
      cursor = { row = start_row, col = start_col },
      total_edits = 0,
      accepted_cost = 0,
      accepted_seq = "",
      accepted_revision = 0,
      can_undo = false,
      can_redo = false,
    },
    recommendations = {},

    -- raw-key capture (InsertEnter atom matching, acceptedSeq replay)
    on_key_buffer = "",

    -- insertion-origin snapshot (set on InsertEnter, cleared on InsertLeave)
    pending = nil,

    -- per-view settings — seeded from the module-level store so
    -- toggles from previous views (within this nvim run) carry over.
    -- `update_setting` writes back into the store on changes.
  }
  local s = settings_store()
  view.display_mode                        = s.display_mode
  view.recommendation_count                = s.recommendation_count
  view.allow_multiple_movements_per_position = s.allow_multiple_movements_per_position
  view.allow_multiple_edits_per_position   = s.allow_multiple_edits_per_position
  view.show_user_typed                     = s.show_user_typed
  view.show_result_count                   = s.show_result_count

  -- Register BEFORE installing keymaps / autocmds / first refresh_ui, so any
  -- handler that dispatches via current_view() finds this view.
  active_by_tab[scratch_tab] = view

  -- Keymap spec is minimal by design — only view-flow keys are
  -- reachable in real time. Everything else (display mode, dedup,
  -- recommendation count, show-user-typed, result-count) lives behind
  -- `gs` which opens the settings modal.
  view.header_handlers = {
    cancel             = function() M.cancel() end,
    undo               = undo,
    redo               = redo,
    open_settings      = function() clear_on_key_buffer(); open_settings_modal() end,
    debug_dump         = function()
      local a = assert_current_view()
      local optimal = (a.result and a.result.optimal_results) or {}
      vim.notify(vim.inspect({
        phase = a.state.phase,
        cursor = a.state.cursor,
        accepted_seq = a.state.accepted_seq,
        accepted_cost = a.state.accepted_cost,
        total_edits = a.state.total_edits,
        can_undo = a.state.can_undo,
        can_redo = a.state.can_redo,
        on_key_buffer = a.on_key_buffer,
        pending = a.pending,
        recs_count = #a.recommendations,
        recs = a.recommendations,
        settings = {
          display_mode = a.display_mode,
          recommendation_count = a.recommendation_count,
          effective_count = a.recommendation_count,
          allow_multiple_movements_per_position = a.allow_multiple_movements_per_position,
          allow_multiple_edits_per_position = a.allow_multiple_edits_per_position,
          show_user_typed = a.show_user_typed,
          show_result_count = a.show_result_count,
        },
        result = {
          user_seq = a.result and a.result.user_seq,
          user_cost = a.result and a.result.user_cost,
          optimal_count = #optimal,
          optimal_results = optimal,
        },
      }), vim.log.levels.INFO)
    end,
  }
  keymaps.install(columns_buf, scratch_buf, list_buf, view.header_handlers)
  keymaps.install_header(summary_buf, view.header_handlers)

  -- CursorMoved forwards any natural motion to the view.
  v.nvim_create_autocmd("CursorMoved", {
    buffer = scratch_buf,
    callback = on_cursor_moved,
    desc = "vimfy explore: forward cursor move to view",
  })

  -- TextChanged fires after a normal-mode edit completes (e.g. `rm`, `x`).
  -- InsertLeave fires after the user exits insert — the right moment to
  -- validate insert-mode edits (`sm<Esc>`, `cl m<Esc>`). TextChangedI is
  -- deliberately NOT hooked: buffer state mid-typing isn't a valid target.
  v.nvim_create_autocmd({ "TextChanged", "InsertLeave" }, {
    buffer = scratch_buf,
    callback = on_buffer_changed,
    desc = "vimfy explore: validate buffer state vs. planned fencepost",
  })

  -- InsertEnter moves the view into Insert so the header can show the
  -- required typed text while the user is in insert mode. Match the edit
  -- atom from on_key_buffer against the
  -- current recommendation set; no-match just defers to InsertLeave.
  v.nvim_create_autocmd("InsertEnter", {
    buffer = scratch_buf,
    callback = on_insert_enter,
    desc = "vimfy explore: begin insert phase",
  })

  -- TextChangedI fires once per keystroke that modifies the buffer in
  -- insert mode. We use it purely for display — live-shrinking the
  -- remaining-text indicator. No FFI calls, no strict-prefix rejection.
  v.nvim_create_autocmd("TextChangedI", {
    buffer = scratch_buf,
    callback = on_insert_text_changed,
    desc = "vimfy explore: live-refresh insert remaining",
  })

  -- Either primary pane closing tears down THIS view. Header panes
  -- are rebuilt dynamically, so we watch `WinClosed` broadly and ignore
  -- internal header-rebuild churn. Pattern-based (not buffer-scoped) so
  -- it survives the scratch buffer being wiped; ID tracked on the view
  -- so destroy_view can delete it explicitly.
  view.winclosed_autocmd = v.nvim_create_autocmd("WinClosed", {
    pattern = "*",
    desc = "vimfy explore: close when a pane closes",
    callback = function(args)
      if view.header.rebuilding then return end
      local match = tonumber(args.match)
      if not match then return end
      if match ~= view.scratch.win and match ~= list_win and match ~= view.header.summary.win then
        local is_header = false
        for _, pane in ipairs(view.header.windows) do
          if pane.win == match then
            is_header = true
            break
          end
        end
        if not is_header then return end
      end
      destroy_view(view)
    end,
  })

  refresh_ui()
  if opts.focus == false and v.nvim_tabpage_is_valid(source_tab) then
    v.nvim_set_current_tabpage(source_tab)
  end
  vim.notify("vimfy explore opened [" .. label .. "]", vim.log.levels.INFO)
  return true
end

function M.cancel()
  local a = current_view()
  if not a then
    vim.notify("vimfy explore not active in current tab", vim.log.levels.WARN)
    return false
  end
  destroy_view(a)
  vim.notify("vimfy explore cancelled", vim.log.levels.INFO)
  return true
end

-- =============================================================================
-- View-scoped settings
-- =============================================================================
-- All the toggles / setters below are ONLY reachable through buffer-local
-- keymaps on the explore scratch buffer (see keymaps.install in M.open).
-- When any of these fires, `current_view()` is guaranteed non-nil — buffer-
-- local keymaps vanish synchronously on teardown, and handlers run
-- synchronously, so there is no window in which the current view could be
-- missing. No defensive check is needed; the invariant is enforced
-- structurally by "keymap-only access, no public M.* export".

---Build the settings schema for the current view and hand it to the
---settings modal. Every set-closure routes through `update_setting` so
---the view AND the module-level store stay in sync (store seeds the
---next view).
function open_settings_modal()
  local a = assert_current_view()
  -- Dedup semantics: the stored field is `allow_multiple_*` (false →
  -- dedup on), but the user-facing label is "dedup". Invert get/set
  -- so the modal shows a natural "on/off" for dedup.
  local function dedup_toggle(flag_key)
    return
      function() return not a[flag_key] end,
      function(on) update_setting(a, flag_key, not on) end
  end
  local motion_get, motion_set = dedup_toggle("allow_multiple_movements_per_position")
  local edit_get, edit_set = dedup_toggle("allow_multiple_edits_per_position")

  local optimal_results = (a.result and a.result.optimal_results) or {}

  local schema = {
    { kind = "setting",
      label = "Display mode",
      value_kind = "enum", values = DISPLAY_MODES,
      get = function() return a.display_mode end,
      set = function(value) update_setting(a, "display_mode", value) end },
    { kind = "setting",
      label = "Recommendation count",
      value_kind = "int", min = RECOMMENDATION_COUNT_MIN, max = RECOMMENDATION_COUNT_MAX,
      get = function() return a.recommendation_count end,
      set = function(value) update_setting(a, "recommendation_count", value) end },
    { kind = "setting",
      label = "Motion dedup",
      value_kind = "bool", get = motion_get, set = motion_set },
    { kind = "setting",
      label = "Edit dedup",
      value_kind = "bool", get = edit_get, set = edit_set },
    { kind = "setting",
      label = "Show user typed",
      value_kind = "bool",
      get = function() return a.show_user_typed end,
      set = function(value) update_setting(a, "show_user_typed", value) end },
    { kind = "setting",
      label = "Optimal results shown",
      value_kind = "int", min = 0, max = #optimal_results,
      get = function() return a.show_result_count end,
      set = function(value) update_setting(a, "show_result_count", value) end },
    { kind = "separator" },
    { kind = "action",
      label = "reset to default settings",
      run = function()
        -- Blow away the sidecar and the in-memory store so the next
        -- `settings_store()` call rebuilds from `config.explore`
        -- (which itself = hardcoded + setup declarations).
        settings_profile.clear("explore")
        current_settings = nil
        local s = settings_store()
        for key in pairs(s) do a[key] = s[key] end
        refresh_ui()
        vim.notify("vimfy explore: settings reset to defaults",
          vim.log.levels.INFO)
      end },
  }
  settings.open(schema, refresh_ui, { title = "Explore Settings" })
end

function M.status()
  local a = current_view()
  if not a then return nil end
  local live_cursor = v.nvim_win_get_cursor(a.scratch.win)
  return {
    phase = vim.deepcopy(a.state.phase),
    is_completed = a.state.is_completed,
    total_edits = a.state.total_edits,
    cursor = vim.deepcopy(a.state.cursor),
    scratch_cursor = { row = live_cursor[1] - 1, col = live_cursor[2] },
    target_range = vim.deepcopy(a.state.target_range),
    accepted_seq = a.state.accepted_seq,
    accepted_cost = a.state.accepted_cost,
    accepted_revision = a.state.accepted_revision,
    can_undo = a.state.can_undo,
    can_redo = a.state.can_redo,
    pending = vim.deepcopy(a.pending),
    session_lines = ffi_lib.explore_current_lines(a.view_id),
    scratch_lines = v.nvim_buf_get_lines(a.scratch.buf, 0, -1, false),
    recommendations = vim.deepcopy(a.recommendations),
  }
end

return M
