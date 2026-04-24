local config = require("vimficiency.config")
local ffi_lib = require("vimficiency.ffi")
local util = require("vimficiency.util")
local keynorm = require("vimficiency.capture.keynorm")
local highlights = require("vimficiency.explore.highlights")
local tags_render = require("vimficiency.explore.render.tags")
local header_render = require("vimficiency.explore.render.header")
local list_render = require("vimficiency.explore.render.list")
local keymaps = require("vimficiency.explore.keymaps")
local settings = require("vimficiency.settings_modal")
local settings_profile = require("vimficiency.settings_profile")

-- Forward-declared locals. Hoisted to the top of the file so every
-- closure below closes over the right upvalue, regardless of the order
-- definitions appear in. Without these, closures defined above the
-- real `local active = ...` line would capture `active` as a global
-- (i.e. nil forever).
---@type VimficiencyExploreActive|nil
local active
local open_settings_modal

-- Layered settings store. On first access we build the layer stack:
--
--   hardcoded defaults  (in config.lua's `fields.explore`)
--        ↓ overlaid by
--   user's init.lua declarations  (already merged into `config.explore`
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

---Update the live session + module store + sidecar file in one shot.
---Auto-save means the user doesn't think about persistence — it just
---happens on every toggle.
---@param key string
---@param value any
local function update_setting(key, value)
  active[key] = value
  local store = settings_store()
  store[key] = value
  settings_profile.save("explore", store)
end

local M = {}

local v = vim.api
-- Only `on_key_ns` is owned by init — the header/list/tags renderers manage
-- their own namespaces via `nvim_create_namespace(name)`, which is
-- idempotent by name so shared-with-renderer namespaces resolve to the
-- same ID without cross-module wiring.
local on_key_ns = v.nvim_create_namespace("vimficiency_explore_on_key")

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
local DISPLAY_MODE_SET = {}
for _, m in ipairs(DISPLAY_MODES) do DISPLAY_MODE_SET[m] = true end

-- Number of recommendations surfaced in the side list + tag overlay.
-- Source of truth is `active.recommendation_count`, seeded from
-- `config.explore.recommendation_count` on first session open. Mutated
-- by the settings modal; stays in-memory across open/close within one
-- nvim run.
local RECOMMENDATION_COUNT_MIN = 1
local RECOMMENDATION_COUNT_MAX = 10

local function current_recommendation_count()
  return active.recommendation_count
end

---@class VimficiencyExploreWindow
---@field buf integer
---@field win integer

---@class VimficiencyExploreHeader
---@field summary VimficiencyExploreWindow
---@field windows VimficiencyExploreWindow[]
---@field rebuilding boolean

---@class VimficiencyExploreScratch
---@field buf integer
---@field win integer
---@field tab integer   # the tab we opened for the session; used on teardown

---@field target string       # the planned typed text we match against
---@field row integer         # buffer row (0-indexed) where insert started
---@field col_start integer   # buffer col (0-indexed bytes) where insert started
---@class VimficiencyExplorePending

---@class VimficiencyExploreActive
---@field label string                                 # caller-supplied, shown in the header
---@field result ResultSession                         # the captured session we're exploring
---@field generation integer                           # FFI handle
---@field header VimficiencyExploreHeader              # fixed panes above the scratch editor
---@field scratch VimficiencyExploreScratch            # scrollable editor pane in the right column
---@field list_buf integer                             # recommendation list buffer (left pane)
---@field state VimficiencyExploreState                # session-reported phase + cursor + seq
---@field recommendations VimficiencyExploreRecommendation[]
---@field on_key_buffer string                         # raw keys captured since last reconcile
---@field pending VimficiencyExplorePending|nil        # insertion-origin snapshot while PendingInsert
---@field display_mode string                          # "off" | "highlight" | "inplace" | "above" | "below"
---@field recommendation_count integer|nil             # settings override, or nil → config default
---@field allow_multiple_motions_per_position boolean  # settings toggle; false → motion recs dedup by landing
---@field allow_multiple_edits_per_position boolean    # settings toggle; false → edit recs dedup by landing
---@field show_user_typed boolean                      # include user's original typed sequence in the header
---@field show_result_count integer                    # how many captured `optimal_results` to include (0 → none)
---@field header_handlers table<string, function>      # keymaps attached to every header pane

local function assert_active()
  assert(active, "vimficiency explore session is not active")
  return active
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

---@param result ResultSession
---@return boolean
---@return string|nil
local function ensure_explore_metadata(result)
  if type(result.goal_lines) ~= "table" or #result.goal_lines == 0 then
    return false, "result is missing goal_lines; re-run the session with a newer vimficiency build"
  end
  if type(result.has_lines_above) ~= "boolean" or type(result.has_lines_below) ~= "boolean" then
    return false, "result is missing explore boundary metadata; re-run the session with a newer vimficiency build"
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
  if active then active.on_key_buffer = "" end
end

local function sync_cursor_to_state()
  local a = assert_active()
  v.nvim_win_set_cursor(a.scratch.win, { a.state.cursor_row + 1, a.state.cursor_col })
end

local function refresh_state_and_recommendations()
  local a = assert_active()
  a.state = ffi_lib.explore_state(a.generation)
  a.recommendations = ffi_lib.explore_recommendations(a.generation,
    current_recommendation_count(),
    a.allow_multiple_motions_per_position,
    a.allow_multiple_edits_per_position)
end

---Compute the suffix of the planned typed text still to be typed. Diffs the
---live scratch buffer against the recorded insertion origin — so the header
---and recommendation list shrink as the user types matching chars, and
---snap back to the full target if the user types a non-matching prefix.
---Falls back to the session's (static) remaining_typed_text when we don't
---have insertion-origin context (e.g. InsertEnter couldn't match an atom).
---@return string
local function current_remaining(a)
  local fallback = a.state.phase.remaining_typed_text or ""
  if a.state.phase.kind ~= "PendingInsert" then return fallback end
  local pending = a.pending
  if not pending or pending.target == "" then return fallback end
  local target = pending.target
  local cursor = v.nvim_win_get_cursor(a.scratch.win)
  local cur_row, cur_col = cursor[1] - 1, cursor[2]
  if cur_row ~= pending.row or cur_col < pending.col_start then return target end
  local line = v.nvim_buf_get_lines(a.scratch.buf, pending.row, pending.row + 1, false)[1] or ""
  local typed_so_far = line:sub(pending.col_start + 1, cur_col)
  -- Longest byte-wise matching prefix of target.
  local i = 0
  while i < #target and i < #typed_so_far
        and target:sub(i + 1, i + 1) == typed_so_far:sub(i + 1, i + 1) do
    i = i + 1
  end
  return target:sub(i + 1)
end

-- For convenience, add a rank field to each recommendation so tag rendering
-- can find them after sorting.
local function attach_ranks()
  local a = assert_active()
  for i, rec in ipairs(a.recommendations) do
    rec.rank = i
  end
end

local function refresh_ui()
  refresh_state_and_recommendations()
  attach_ranks()
  sync_cursor_to_state()
  local remaining = current_remaining(active)
  header_render.render(active, remaining)
  list_render.render(active, remaining)
  tags_render.render(active)
end

---Rewrite the scratch buffer to match the session's current lines. The buffer
---stays modifiable throughout — natural edit commands must be able to fire
---between sync calls.
local function sync_buffer_from_session()
  local a = assert_active()
  local lines = ffi_lib.explore_current_lines(a.generation)
  v.nvim_buf_set_lines(a.scratch.buf, 0, -1, false, lines)
  vim.bo[a.scratch.buf].modified = false
end

local function undo()
  clear_on_key_buffer()
  local a = assert_active()
  local result = ffi_lib.explore_undo(a.generation)
  if result.status == "Rejected" then
    vim.notify("vimficiency explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  sync_buffer_from_session()
  refresh_ui()
  return true
end

local function redo()
  clear_on_key_buffer()
  local a = assert_active()
  local result = ffi_lib.explore_redo(a.generation)
  if result.status == "Rejected" then
    vim.notify("vimficiency explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  sync_buffer_from_session()
  refresh_ui()
  return true
end

local function on_cursor_moved()
  local a = assert_active()
  -- Loop-avoidance: if nvim cursor already matches session cursor, we just set it
  -- ourselves (e.g. after apply_motion / undo / redo) — nothing to forward.
  local pos = v.nvim_win_get_cursor(a.scratch.win)
  local new_row, new_col = pos[1] - 1, pos[2]
  if new_row == a.state.cursor_row and new_col == a.state.cursor_col then
    return
  end

  local raw_keys = a.on_key_buffer or ""
  a.on_key_buffer = ""
  local applied = ffi_lib.explore_accept_cursor_move(
    a.generation, new_row, new_col, raw_keys)
  if applied.status == "Rejected" then
    -- Session refused the external move — snap cursor back to the session's view.
    sync_cursor_to_state()
    return
  end
  refresh_ui()
end

---InsertEnter handler — transition the session into PendingInsert when we
---recognize which edit atom the user just triggered. The atom is identified
---by tail-matching the raw-key buffer against known edit recommendations;
---the matched rec's `typed_text` becomes the `remainingText` the header
---displays to the user. On no match, we do nothing — InsertLeave's
---`accept_insert_exit` fallback still validates the final buffer.
local function on_insert_enter()
  local a = assert_active()
  -- Reentrancy: nested <C-o>-style inserts shouldn't re-call beginEdit.
  if a.state.phase.kind ~= "ApproachEdit" then return end

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
  local applied = ffi_lib.explore_begin_edit(
    a.generation, true, matched.typed_text or "")
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
  local a = assert_active()
  if a.state.phase.kind ~= "PendingInsert" then return end
  local remaining = current_remaining(a)
  header_render.render(a, remaining)
  list_render.render(a, remaining)
end

---Strict-revert buffer-state handler. Called on TextChanged (normal mode) and
---InsertLeave. Branches on phase: in PendingInsert the buffer is checked
---against the planned post-edit fencepost via acceptInsertExit; in
---ApproachEdit the legacy acceptBufferState path still handles normal-mode
---edits (r, x, dd, …). Rejections revert the scratch to the session's
---last-known lines + cursor.
local function on_buffer_changed()
  local a = assert_active()
  local new_lines = v.nvim_buf_get_lines(a.scratch.buf, 0, -1, false)
  local pos = v.nvim_win_get_cursor(a.scratch.win)
  local new_row, new_col = pos[1] - 1, pos[2]

  local session_lines = ffi_lib.explore_current_lines(a.generation)
  local buffer_matches_session = (#new_lines == #session_lines)
  if buffer_matches_session then
    for i = 1, #new_lines do
      if new_lines[i] ~= session_lines[i] then
        buffer_matches_session = false
        break
      end
    end
  end

  if a.state.phase.kind == "PendingInsert" then
    -- Clear the insertion-origin snapshot — we're leaving PendingInsert on
    -- either branch below, so the TextChangedI live-remaining computation
    -- should stop until the next InsertEnter sets it again.
    a.pending = nil

    -- Abandoned insert: user exited without net change. Roll phase back
    -- without polluting redo so we're cleanly in ApproachEdit again.
    if buffer_matches_session then
      a.on_key_buffer = ""
      ffi_lib.explore_cancel_pending_insert(a.generation)
      refresh_ui()
      return
    end
    local raw_keys = a.on_key_buffer or ""
    a.on_key_buffer = ""
    local applied = ffi_lib.explore_accept_insert_exit(
      a.generation, new_lines, new_row, new_col, raw_keys)
    if applied.status == "Applied" then
      refresh_ui()
      return
    end
    -- Reject: cancel the pending phase, revert buffer to pre-edit fencepost.
    vim.notify("vimficiency explore: " .. applied.reason, vim.log.levels.WARN)
    ffi_lib.explore_cancel_pending_insert(a.generation)
    sync_buffer_from_session()
    refresh_ui()
    return
  end

  -- ApproachEdit / Completed — existing strict path.
  if buffer_matches_session then return end

  local raw_keys = a.on_key_buffer or ""
  a.on_key_buffer = ""

  local applied = ffi_lib.explore_accept_buffer_state(
    a.generation, new_lines, new_row, new_col, raw_keys)

  if applied.status == "Rejected" then
    -- Strict (b): revert the scratch to the session's last known state + cursor.
    vim.notify("vimficiency explore: " .. applied.reason, vim.log.levels.WARN)
    sync_buffer_from_session()
    sync_cursor_to_state()
    return
  end
  refresh_ui()
end

local function destroy_active_and_tab()
  if not active then return end
  local tab = active.scratch.tab
  vim.on_key(nil, on_key_ns)
  ffi_lib.explore_destroy(active.generation)
  active = nil
  vim.schedule(function()
    if tab and v.nvim_tabpage_is_valid(tab) then
      pcall(function()
        v.nvim_set_current_tabpage(tab)
        vim.cmd("tabclose")
      end)
    end
  end)
end

function M.open(label, result, opts)
  assert(type(label) == "string" and label ~= "", "explore.open: label must be non-empty")
  assert(type(result) == "table", "explore.open: result must be a ResultSession")

  -- Re-blend against the current Normal group in case the colorscheme changed.
  highlights.refresh()

  local ok, err = ensure_explore_metadata(result)
  if not ok then
    vim.notify("vimficiency explore failed: " .. err, vim.log.levels.ERROR)
    return false
  end

  if active then
    M.cancel()
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

  local generation = ffi_lib.explore_start(
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
  -- undolevels = -1 disables Vim's native undo so the session is authoritative
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
  assert(columns_win, "vimficiency explore: failed to create header row")
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
  assert(summary_win, "vimficiency explore: failed to create summary header")
  local summary_buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(summary_win, summary_buf)
  v.nvim_set_current_win(scratch_win)

  active = {
    -- identity
    label = label,
    result = result,
    generation = generation,

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
      phase = { kind = "ApproachEdit", edit_index = 0, remaining_typed_text = "" },
      cursor_row = start_row,
      cursor_col = start_col,
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

    -- per-session settings — seeded from the module-level store so
    -- toggles from previous sessions (within this nvim run) carry over.
    -- `update_setting` writes back into the store on changes.
  }
  local s = settings_store()
  active.display_mode                        = s.display_mode
  active.recommendation_count                = s.recommendation_count
  active.allow_multiple_motions_per_position = s.allow_multiple_motions_per_position
  active.allow_multiple_edits_per_position   = s.allow_multiple_edits_per_position
  active.show_user_typed                     = s.show_user_typed
  active.show_result_count                   = s.show_result_count

  -- Capture raw key bytes only while this session is open. We can't prevent
  -- Vim from handling the key — we only observe it so that CursorMoved can
  -- forward a motion with the text that produced it. The normalization step
  -- is shared with the regular mark/watch/recall capture — see
  -- dev/lua/key-normalization.md.
  vim.on_key(function(_, typed)
    if not (active and typed and typed ~= "") then return end
    active.on_key_buffer = active.on_key_buffer .. keynorm.normalize(typed)
  end, on_key_ns)

  -- Keymap spec is minimal by design — only session-flow keys are
  -- reachable in real time. Everything else (display mode, dedup,
  -- recommendation count, show-user-typed, result-count) lives behind
  -- `gs` which opens the settings modal.
  active.header_handlers = {
    cancel             = function() M.cancel() end,
    undo               = undo,
    redo               = redo,
    open_settings      = function() clear_on_key_buffer(); open_settings_modal() end,
    debug_dump         = function()
      local a = assert_active()
      local optimal = (a.result and a.result.optimal_results) or {}
      vim.notify(vim.inspect({
        phase = a.state.phase,
        cursor = { row = a.state.cursor_row, col = a.state.cursor_col },
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
          effective_count = current_recommendation_count(),
          allow_multiple_motions_per_position = a.allow_multiple_motions_per_position,
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
  keymaps.install(columns_buf, scratch_buf, list_buf, active.header_handlers)
  keymaps.install_header(summary_buf, active.header_handlers)

  -- CursorMoved forwards any natural motion to the session.
  v.nvim_create_autocmd("CursorMoved", {
    buffer = scratch_buf,
    callback = on_cursor_moved,
    desc = "vimficiency explore: forward cursor move to session",
  })

  -- TextChanged fires after a normal-mode edit completes (e.g. `rm`, `x`).
  -- InsertLeave fires after the user exits insert — the right moment to
  -- validate insert-mode edits (`sm<Esc>`, `cl m<Esc>`). TextChangedI is
  -- deliberately NOT hooked: buffer state mid-typing isn't a valid target.
  v.nvim_create_autocmd({ "TextChanged", "InsertLeave" }, {
    buffer = scratch_buf,
    callback = on_buffer_changed,
    desc = "vimficiency explore: validate buffer state vs. planned fencepost",
  })

  -- InsertEnter moves the session from ApproachEdit into PendingInsert so
  -- the header can show the required typed text while the user is in
  -- insert mode. Match the edit atom from on_key_buffer against the
  -- current recommendation set; no-match just defers to InsertLeave.
  v.nvim_create_autocmd("InsertEnter", {
    buffer = scratch_buf,
    callback = on_insert_enter,
    desc = "vimficiency explore: begin pending-insert phase",
  })

  -- TextChangedI fires once per keystroke that modifies the buffer in
  -- insert mode. We use it purely for display — live-shrinking the
  -- remaining-text indicator. No FFI calls, no strict-prefix rejection.
  v.nvim_create_autocmd("TextChangedI", {
    buffer = scratch_buf,
    callback = on_insert_text_changed,
    desc = "vimficiency explore: live-refresh pending-insert remaining",
  })

  -- Either primary pane closing tears down the whole session. Header panes
  -- are rebuilt dynamically, so we watch `WinClosed` broadly and ignore
  -- internal header-rebuild churn.
  local function on_window_close(args)
    if not active then return end
    if active.header.rebuilding then return end
    local match = tonumber(args.match)
    if not match then return end
    if match ~= active.scratch.win and match ~= list_win and match ~= active.header.summary.win then
      local is_header = false
      for _, pane in ipairs(active.header.windows) do
        if pane.win == match then
          is_header = true
          break
        end
      end
      if not is_header then return end
    end
    destroy_active_and_tab()
  end
  v.nvim_create_autocmd("WinClosed", {
    pattern = "*",
    callback = on_window_close,
    desc = "vimficiency explore: close when a pane closes",
  })

  refresh_ui()
  if opts.focus == false and v.nvim_tabpage_is_valid(source_tab) then
    v.nvim_set_current_tabpage(source_tab)
  end
  vim.notify("vimficiency explore opened [" .. label .. "]", vim.log.levels.INFO)
  return true
end

function M.cancel()
  if not active then
    vim.notify("vimficiency explore not active", vim.log.levels.WARN)
    return false
  end
  destroy_active_and_tab()
  vim.notify("vimficiency explore cancelled", vim.log.levels.INFO)
  return true
end

-- =============================================================================
-- Session-scoped settings
-- =============================================================================
-- All the toggles / setters below are ONLY reachable through buffer-local
-- keymaps on the explore scratch buffer (see keymaps.install in M.open).
-- When any of these fires, `active` is guaranteed set — buffer-local
-- keymaps vanish synchronously on teardown, and handlers run synchronously,
-- so there is no window in which `active` could be nil. No defensive check
-- is needed; the invariant is enforced structurally by "keymap-only
-- access, no public M.* export".

---Build the settings schema for the active session and hand it to the
---settings modal. Every set-closure routes through `update_setting` so
---`active` AND the module-level store stay in sync (store seeds the
---next session).
function open_settings_modal()
  local a = assert_active()
  -- Dedup semantics: the stored field is `allow_multiple_*` (false →
  -- dedup on), but the user-facing label is "dedup". Invert get/set
  -- so the modal shows a natural "on/off" for dedup.
  local function dedup_toggle(flag_key)
    return
      function() return not a[flag_key] end,
      function(on) update_setting(flag_key, not on) end
  end
  local motion_get, motion_set = dedup_toggle("allow_multiple_motions_per_position")
  local edit_get, edit_set = dedup_toggle("allow_multiple_edits_per_position")

  local optimal_results = (a.result and a.result.optimal_results) or {}

  local schema = {
    { kind = "setting",
      label = "Display mode",
      value_kind = "enum", values = DISPLAY_MODES,
      get = function() return a.display_mode end,
      set = function(v) update_setting("display_mode", v) end },
    { kind = "setting",
      label = "Recommendation count",
      value_kind = "int", min = RECOMMENDATION_COUNT_MIN, max = RECOMMENDATION_COUNT_MAX,
      get = function() return current_recommendation_count() end,
      set = function(v) update_setting("recommendation_count", v) end },
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
      set = function(v) update_setting("show_user_typed", v) end },
    { kind = "setting",
      label = "Optimal results shown",
      value_kind = "int", min = 0, max = #optimal_results,
      get = function() return a.show_result_count end,
      set = function(v) update_setting("show_result_count", v) end },
    { kind = "separator" },
    { kind = "action",
      label = "reset to default settings",
      run = function()
        -- Blow away the sidecar and the in-memory store so the next
        -- `settings_store()` call rebuilds from `config.explore`
        -- (which itself = hardcoded + init.lua declarations).
        settings_profile.clear("explore")
        current_settings = nil
        local s = settings_store()
        for key in pairs(s) do a[key] = s[key] end
        refresh_ui()
        vim.notify("vimficiency explore: settings reset to defaults",
          vim.log.levels.INFO)
      end },
  }
  settings.open(schema, refresh_ui, { title = "Explore Settings" })
end

function M.status()
  if not active then return nil end
  return {
    phase = vim.deepcopy(active.state.phase),
    accepted_seq = active.state.accepted_seq,
    accepted_cost = active.state.accepted_cost,
    recommendations = vim.deepcopy(active.recommendations),
  }
end

return M
