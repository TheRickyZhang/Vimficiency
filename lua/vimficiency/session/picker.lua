-- Interactive session picker: Telescope-flavored float + preview.
--
-- Two panes: Active (in-memory sessions from `session_store.summarize_all`)
-- and Saved (files under stdpath("data")/vimficiency/saved). `<Tab>` toggles.
-- Ongoing entries are always pinned to a top section; sort applies within
-- each section.

local v = vim.api
local uv = vim.uv

local session = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local alias_mod = require("vimficiency.session.alias")
local highlights = require("vimficiency.highlights")
local sequence_display = require("vimficiency.sequence_display")
local util = require("vimficiency.util")

-- Namespace for picker-owned extmarks (header pane indicator, section
-- dividers). Cleared and re-populated on every render().
local picker_ns = v.nvim_create_namespace("vimficiency.session.picker")

local M = {}

local SORT_MODES = { "alpha", "category", "created" }

--- Natural direction for each sort mode: alpha/category ascend (A→Z);
--- created descends (newest first). Capital suffix flips these defaults.
local DEFAULT_DIRECTIONS = {
  alpha    = "asc",
  category = "asc",
  created  = "desc",
}

local STATUS_SYMBOLS = { ongoing = "●", saved = "✓", blank = " " }

local SORT_POPUP_DELAY_MS = 250

-- Singleton state: nil when closed.
---@class PickerState
---@field win      integer       # Outer float window id
---@field buf      integer       # List buffer id
---@field prev_win integer       # Preview (vsplit) window id
---@field prev_buf integer       # Preview buffer id
---@field pane      "active"|"saved"
---@field sort_mode string        # One of SORT_MODES
---@field direction "asc"|"desc"
---@field query     string
---@field marked   table<string, boolean>  -- keyed by item.key (scoped per pane via prefix)
---@field rows     table[]       # row index -> { item = ..., is_header = bool }
---@field cursor   integer       # 1-indexed row in the list buffer
local state = nil

--------------------------------------------------------------------------------
-- Helpers
--------------------------------------------------------------------------------

local function save_dir()
  return vim.fn.stdpath("data") .. "/vimficiency/saved"
end

local function format_age_ns(t_ns)
  if not t_ns or t_ns == 0 then return "?" end
  local diff = (uv.hrtime() - t_ns) / 1e9
  if diff < 1 then return "just now" end
  if diff < 60 then return string.format("%ds ago", math.floor(diff)) end
  if diff < 3600 then return string.format("%dm ago", math.floor(diff / 60)) end
  if diff < 86400 then return string.format("%dh ago", math.floor(diff / 3600)) end
  return string.format("%dd ago", math.floor(diff / 86400))
end

local function format_epoch(sec)
  if not sec or sec == 0 then return "?" end
  return os.date("%Y-%m-%d %H:%M", sec)
end

local function fuzzy_match(haystack, needle)
  if not needle or needle == "" then return true end
  local h = haystack:lower()
  local n = needle:lower()
  local hi, ni = 1, 1
  while hi <= #h and ni <= #n do
    if h:sub(hi, hi) == n:sub(ni, ni) then ni = ni + 1 end
    hi = hi + 1
  end
  return ni > #n
end

local function mark_key(item)
  return item.pane .. ":" .. item.key
end

--------------------------------------------------------------------------------
-- Item builders
--------------------------------------------------------------------------------

local function build_active_items()
  local summaries = session_store.summarize_all()
  local items = {}
  -- Active recall-type sessions are auto-generated per keystroke and would
  -- flood the list. Fold them into a single synthetic summary row.
  local recall_ring = {}
  for _, s in ipairs(summaries) do
    if s.type == "recall" and s.status == "active" then
      table.insert(recall_ring, s)
    else
      table.insert(items, {
        pane          = "active",
        key           = s.id,
        name          = s.display_alias or s.id,
        category      = s.type,
        status        = s.status,
        start_time_ns = s.start_time,
        end_time_ns   = s.end_time,
        key_count     = s.key_count,
        preview_seq   = s.preview,
        result        = s.result,
        summary       = s,
      })
    end
  end
  if #recall_ring > 0 then
    local oldest_ns, newest_ns = math.huge, -math.huge
    local total_keys = 0
    for _, s in ipairs(recall_ring) do
      if s.start_time < oldest_ns then oldest_ns = s.start_time end
      if s.start_time > newest_ns then newest_ns = s.start_time end
      total_keys = total_keys + (s.key_count or 0)
    end
    table.insert(items, {
      pane           = "active",
      key            = "__recall_ring__",
      name           = "recall ring",
      category       = "recall",
      status         = "active",
      is_synthetic   = true,
      -- Sort by newest so the summary sits near other recent activity.
      start_time_ns  = newest_ns,
      ring_count     = #recall_ring,
      ring_oldest_ns = oldest_ns,
      ring_newest_ns = newest_ns,
      ring_total_keys = total_keys,
    })
  end
  return items
end

local function build_saved_items()
  local names = session.list_saved()
  local items = {}
  for _, name in ipairs(names) do
    local path = save_dir() .. "/" .. name .. ".json"
    local stat = uv.fs_stat(path)
    local mtime_sec = stat and stat.mtime.sec or 0
    table.insert(items, {
      pane      = "saved",
      key       = name,
      name      = name,
      category  = "saved",
      status    = "finished",
      mtime_sec = mtime_sec,
      path      = path,
    })
  end
  return items
end

--------------------------------------------------------------------------------
-- Grouping + rendering
--------------------------------------------------------------------------------

--- Sort `items` ascending by `mode`, then reverse in place if direction == "desc".
local function sort_items(items, mode, direction)
  local function cmp_asc(a, b)
    if mode == "alpha" then
      return a.name < b.name
    elseif mode == "created" then
      -- Ascending = oldest first.
      local at = a.start_time_ns or (a.mtime_sec or 0) * 1e9
      local bt = b.start_time_ns or (b.mtime_sec or 0) * 1e9
      return at < bt
    else -- category
      if (a.category or "") ~= (b.category or "") then
        return (a.category or "") < (b.category or "")
      end
      return a.name < b.name
    end
  end
  table.sort(items, cmp_asc)
  if direction == "desc" then
    local n = #items
    for i = 1, math.floor(n / 2) do
      items[i], items[n - i + 1] = items[n - i + 1], items[i]
    end
  end
end

--- Split items into sections: Ongoing (pinned top) + either categories or a
--- single "All" section depending on sort.
local function group_into_sections(items, mode, direction)
  local ongoing, rest = {}, {}
  for _, it in ipairs(items) do
    if it.pane == "active" and it.status == "active" then
      table.insert(ongoing, it)
    else
      table.insert(rest, it)
    end
  end
  sort_items(ongoing, mode, direction)
  sort_items(rest, mode, direction)

  local sections = {}
  if #ongoing > 0 then
    table.insert(sections, { title = "Ongoing", items = ongoing })
  end
  if mode == "category" then
    local by_cat = {}
    local order = {}
    for _, it in ipairs(rest) do
      local c = it.category or "other"
      if not by_cat[c] then
        by_cat[c] = {}
        table.insert(order, c)
      end
      table.insert(by_cat[c], it)
    end
    table.sort(order)
    for _, c in ipairs(order) do
      table.insert(sections, { title = c, items = by_cat[c] })
    end
  elseif #rest > 0 then
    table.insert(sections, { title = "All", items = rest })
  end
  return sections
end

local function row_for_item(item)
  local sym
  if item.pane == "active" and item.status == "active" then
    sym = STATUS_SYMBOLS.ongoing
  elseif item.pane == "saved" then
    sym = STATUS_SYMBOLS.saved
  else
    sym = STATUS_SYMBOLS.blank
  end
  local marked = item.is_synthetic and " " or (state.marked[mark_key(item)] and "*" or " ")
  local cat = item.category or ""
  if item.is_synthetic then
    local name = string.format("%s (%d)", item.name, item.ring_count)
    local age = string.format("oldest %s", format_age_ns(item.ring_oldest_ns))
    return string.format("  %s %s %-16s  %-8s  %s", marked, sym, name, cat, age)
  end
  local age
  if item.pane == "active" then
    age = format_age_ns(item.start_time_ns)
  else
    age = format_epoch(item.mtime_sec)
  end
  return string.format("  %s %s %-16s  %-8s  %s", marked, sym, item.name, cat, age)
end

local function render()
  if not state then return end
  local pane_items = (state.pane == "active") and build_active_items() or build_saved_items()

  -- Apply fuzzy filter.
  local filtered = {}
  for _, it in ipairs(pane_items) do
    if fuzzy_match(it.name, state.query) then
      table.insert(filtered, it)
    end
  end

  local sections = group_into_sections(filtered, state.sort_mode, state.direction)

  -- Float's window title already says "Vimficiency Sessions"; header here
  -- just shows the two orthogonal controls (pane + sort). Highlight spans
  -- are computed alongside the line so the selected pane label stands out.
  local arrow = state.direction == "asc" and "↑" or "↓"
  local pane_active_label = "Active"
  local pane_saved_label = "Saved"
  local header_prefix = " pane: "
  local header_sep = "  "
  local sort_label = string.format("  sort: %s %s", arrow, state.sort_mode)
  local header_line = header_prefix
      .. pane_active_label
      .. header_sep
      .. pane_saved_label
      .. sort_label

  -- Byte offsets of each stylable span on the header line. Lua 1-indexed
  -- string ops; nvim_buf_set_extmark wants 0-indexed byte offsets.
  local active_start = #header_prefix
  local active_end = active_start + #pane_active_label
  local saved_start = active_end + #header_sep
  local saved_end = saved_start + #pane_saved_label
  local sort_start = saved_end
  local sort_end = #header_line

  local header_lines = {
    header_line,
    string.rep("─", math.max(10, v.nvim_win_get_width(state.win) / 2 - 2)),
  }

  local lines = {}
  for _, l in ipairs(header_lines) do table.insert(lines, l) end

  local rows = {}   -- parallel to `lines`; nil for non-item rows
  for _, _ in ipairs(header_lines) do table.insert(rows, { is_header = true }) end

  -- Track extmark ranges we'll apply after set_lines. Each entry is
  -- `{line_idx, col_start, col_end, hl_group}` using 0-indexed coordinates.
  local hl_spans = {
    { 0, 0, active_start, highlights.PICKER_HEADER_DIM },  -- "pane: "
    { 0, active_start, active_end,
      state.pane == "active"
        and highlights.PICKER_PANE_ACTIVE
        or  highlights.PICKER_PANE_INACTIVE },
    { 0, active_end, saved_start, highlights.PICKER_HEADER_DIM },
    { 0, saved_start, saved_end,
      state.pane == "saved"
        and highlights.PICKER_PANE_ACTIVE
        or  highlights.PICKER_PANE_INACTIVE },
    { 0, sort_start, sort_end, highlights.PICKER_HEADER_DIM },
  }

  for si, section in ipairs(sections) do
    if si > 1 then
      table.insert(lines, "")
      table.insert(rows, { is_header = true })
    end
    local section_line = "── " .. section.title .. " ──"
    table.insert(lines, section_line)
    table.insert(rows, { is_header = true })
    table.insert(hl_spans,
      { #lines - 1, 0, #section_line, highlights.PICKER_SECTION })
    for _, it in ipairs(section.items) do
      table.insert(lines, row_for_item(it))
      table.insert(rows, { item = it })
    end
  end

  if #rows == #header_lines then
    table.insert(lines, "")
    table.insert(lines, "  (no entries — press ? for help)")
    table.insert(rows, { is_header = true })
    table.insert(rows, { is_header = true })
  end

  vim.bo[state.buf].modifiable = true
  v.nvim_buf_set_lines(state.buf, 0, -1, false, lines)
  vim.bo[state.buf].modifiable = false

  -- Re-apply header + section highlights. Clearing the namespace first is
  -- necessary because nvim_buf_set_lines doesn't clear extmarks owned by
  -- our ns (they'd stack across renders and drift as rows shift).
  v.nvim_buf_clear_namespace(state.buf, picker_ns, 0, -1)
  for _, span in ipairs(hl_spans) do
    local line_idx, col_start, col_end, hl_group = span[1], span[2], span[3], span[4]
    v.nvim_buf_set_extmark(state.buf, picker_ns, line_idx, col_start, {
      end_row = line_idx,
      end_col = col_end,
      hl_group = hl_group,
    })
  end

  state.rows = rows

  -- Clamp cursor onto a selectable row if possible.
  local target = state.cursor or 1
  target = math.max(1, math.min(target, #lines))
  -- Move to nearest item row if current is a header.
  if rows[target] and rows[target].is_header then
    for delta = 1, #lines do
      local up, down = target - delta, target + delta
      if up >= 1 and rows[up] and rows[up].item then target = up; break end
      if down <= #lines and rows[down] and rows[down].item then target = down; break end
    end
  end
  state.cursor = target
  pcall(v.nvim_win_set_cursor, state.win, { target, 0 })

  M.update_preview()
end

--------------------------------------------------------------------------------
-- Preview pane
--------------------------------------------------------------------------------

local function preview_lines_for_active(item)
  if item.is_synthetic then
    return {
      "Recall ring (auto-generated key/time windows)",
      "",
      string.format("Entries:    %d", item.ring_count),
      string.format("Newest:     %s", format_age_ns(item.ring_newest_ns)),
      string.format("Oldest:     %s", format_age_ns(item.ring_oldest_ns)),
      string.format("Total keys: %d", item.ring_total_keys),
      "",
      "The ring is populated on every keystroke; entries are not directly",
      "actionable from this picker. Resolve a specific window via:",
      "  :Vimfy recall N    — N keys ago",
      "  :Vimfy recall Ns   — N seconds ago",
    }
  end
  local lines = {
    string.format("Alias:    %s", item.name),
    string.format("Type:     %s", item.category or "?"),
    string.format("Status:   %s", item.status),
    string.format("Started:  %s", format_age_ns(item.start_time_ns)),
  }
  if item.end_time_ns then
    table.insert(lines, string.format("Finished: %s", format_age_ns(item.end_time_ns)))
  end
  table.insert(lines, string.format("Keys:     %d", item.key_count or 0))
  if item.preview_seq and item.preview_seq ~= "" then
    table.insert(lines, "")
    table.insert(lines, "User seq:")
    table.insert(lines, "  " .. item.preview_seq)
  end
  local result = item.result
  if result then
    table.insert(lines, "")
    local user_cost = result.user_cost and string.format(" (cost: %.2f)", result.user_cost) or ""
    vim.list_extend(lines,
      sequence_display.prefixed_lines("User full: ", result.user_seq or "", nil, user_cost))
    table.insert(lines, "")
    table.insert(lines, "Optimal:")
    for i, r in ipairs(result.optimal_results or {}) do
      if i > 5 then break end
      vim.list_extend(lines,
        sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq or "", nil,
          string.format("  (cost: %.2f)", r.cost or 0)))
    end
  end
  return lines
end

local function preview_lines_for_saved(item)
  local lines = {
    string.format("Name:  %s", item.name),
    string.format("Path:  %s", vim.fn.fnamemodify(item.path, ":~")),
    string.format("Saved: %s", format_epoch(item.mtime_sec)),
    "",
  }
  local raw = vim.fn.readfile(item.path)
  local ok, data = pcall(vim.json.decode, table.concat(raw, "\n"))
  if not ok or type(data) ~= "table" then
    table.insert(lines, "(unable to parse file)")
    return lines
  end
  table.insert(lines, string.format("Position: (%d,%d) -> (%d,%d)",
    data.start_row or 0, data.start_col or 0, data.end_row or 0, data.end_col or 0))
  local user_cost = data.user_cost and string.format(" (cost: %.2f)", data.user_cost) or ""
  if data.user_seq and data.user_seq ~= "" then
    vim.list_extend(lines,
      sequence_display.prefixed_lines("User seq: ", data.user_seq, nil, user_cost))
  else
    table.insert(lines, "User seq: (none)" .. user_cost)
  end
  table.insert(lines, "")
  table.insert(lines, "Optimal:")
  for i, r in ipairs(data.optimal_results or {}) do
    if i > 5 then break end
    vim.list_extend(lines,
      sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq or "", nil,
        string.format("  (cost: %.2f)", r.cost or 0)))
  end
  return lines
end

function M.update_preview()
  if not state or not v.nvim_buf_is_valid(state.prev_buf) then return end
  local row = state.cursor
  local entry = state.rows[row]
  local lines
  if not entry or entry.is_header then
    lines = { "(no selection)" }
  elseif entry.item.pane == "active" then
    lines = preview_lines_for_active(entry.item)
  else
    lines = preview_lines_for_saved(entry.item)
  end
  vim.bo[state.prev_buf].modifiable = true
  v.nvim_buf_set_lines(state.prev_buf, 0, -1, false, lines)
  vim.bo[state.prev_buf].modifiable = false
end

--------------------------------------------------------------------------------
-- Current-row helpers
--------------------------------------------------------------------------------

local function current_item()
  if not state then return nil end
  local row = v.nvim_win_get_cursor(state.win)[1]
  state.cursor = row
  local entry = state.rows[row]
  if entry and entry.item then return entry.item end
  return nil
end

--------------------------------------------------------------------------------
-- Actions
--------------------------------------------------------------------------------

local function close()
  if not state then return end
  local win = state.win
  local prev_win = state.prev_win
  local prompt_win = state.prompt_win
  state = nil
  if v.nvim_win_is_valid(prompt_win) then pcall(v.nvim_win_close, prompt_win, true) end
  if v.nvim_win_is_valid(prev_win) then pcall(v.nvim_win_close, prev_win, true) end
  if v.nvim_win_is_valid(win) then pcall(v.nvim_win_close, win, true) end
end

local function act_toggle_pane()
  state.pane = (state.pane == "active") and "saved" or "active"
  state.cursor = 1
  render()
end

--- Apply a sort mode. `reverse` flips away from the mode's natural direction.
local function act_sort_apply(mode, reverse)
  state.sort_mode = mode
  local natural = DEFAULT_DIRECTIONS[mode] or "asc"
  if reverse then
    state.direction = (natural == "asc") and "desc" or "asc"
  else
    state.direction = natural
  end
  render()
end

local SORT_HINT_LINES = {
  "  Sort by…",
  "",
  "  n / N   name       (A→Z / Z→A)",
  "  c / C   category   (A→Z / Z→A)",
  "  t / T   time       (newest / oldest)",
  "",
  "  <Esc>   cancel",
}

local function show_sort_popup()
  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  v.nvim_buf_set_lines(buf, 0, -1, false, SORT_HINT_LINES)
  vim.bo[buf].modifiable = false
  local width = 44
  local height = #SORT_HINT_LINES
  local row = math.floor((vim.o.lines - height) / 2)
  local col = math.floor((vim.o.columns - width) / 2)
  local win = v.nvim_open_win(buf, false, {
    relative = "editor", row = row, col = col,
    width = width, height = height, border = "rounded", style = "minimal",
    title = " Sort ", title_pos = "center", focusable = false,
  })
  vim.cmd("redraw")
  local ok, ch = pcall(vim.fn.getcharstr)
  pcall(v.nvim_win_close, win, true)
  if not ok or ch == "" or ch == "\27" then return end
  local mode_of = { n = "alpha", c = "category", t = "created" }
  local m = mode_of[ch:lower()]
  if not m then return end
  act_sort_apply(m, ch:match("%u") ~= nil)
end

-- Plain `s` falls through to the hint popup once the mapping engine resolves
-- the ambiguous prefix (after 'timeoutlen'). Fast typers never see it.
local act_sort_popup = show_sort_popup

local function act_search()
  if not state or not v.nvim_win_is_valid(state.prompt_win) then return end
  v.nvim_set_current_win(state.prompt_win)
  -- startinsert! lands cursor at end-of-line, preserving any existing query.
  vim.cmd("startinsert!")
end

--- Move the list cursor by `delta`, skipping header rows. Used from prompt
--- keymaps so the user can navigate without leaving insert mode.
local function move_list(delta)
  if not state or not v.nvim_win_is_valid(state.win) then return end
  local cur = v.nvim_win_get_cursor(state.win)[1]
  local max = v.nvim_buf_line_count(state.buf)
  local target = cur + delta
  local step = delta >= 0 and 1 or -1
  while target >= 1 and target <= max
        and state.rows[target] and state.rows[target].is_header do
    target = target + step
  end
  if target < 1 or target > max then return end
  pcall(v.nvim_win_set_cursor, state.win, { target, 0 })
  state.cursor = target
  M.update_preview()
end

--- Leave the prompt and return focus to the list in normal mode.
local function exit_prompt_to_list()
  vim.cmd("stopinsert")
  if state and v.nvim_win_is_valid(state.win) then
    v.nvim_set_current_win(state.win)
  end
end

local function act_mark()
  local it = current_item()
  if not it then return end
  if it.is_synthetic then return end
  local k = mark_key(it)
  state.marked[k] = not state.marked[k] or nil
  -- Advance one row for rapid marking.
  local row = state.cursor + 1
  local max = v.nvim_buf_line_count(state.buf)
  while row <= max and state.rows[row] and state.rows[row].is_header do
    row = row + 1
  end
  if row <= max then state.cursor = row end
  render()
end

local function act_open()
  local it = current_item()
  if not it then return end
  if it.is_synthetic then
    vim.notify("Recall ring is not directly openable. Use ':Vimfy recall N' or ':Vimfy recall Ns'.",
      vim.log.levels.INFO)
    return
  end
  if it.pane == "saved" then
    -- Load into workspace under a manual alias (default: the filename if alphabetic).
    local function fetch_under(alias)
      if not alias_mod.is_valid_manual(alias) then
        vim.notify("Need an alphabetic alias (got '" .. alias .. "')", vim.log.levels.ERROR)
        return
      end
      session.fetch(it.name, alias)
      close()
      -- Reopen on Active pane so the user sees the newly fetched entry.
      vim.schedule(function()
        M.open()
        if state then state.pane = "active"; render() end
      end)
    end
    if alias_mod.is_valid_manual(it.name) then
      fetch_under(it.name)
    else
      vim.ui.input({ prompt = "Fetch [" .. it.name .. "] as alias: " }, function(input)
        if not input or input == "" then return end
        fetch_under(input)
      end)
    end
    return
  end
  -- Active pane.
  if it.status == "active" then
    vim.notify("Session [" .. it.name .. "] is still running. Finish it with ':Vimfy finish " ..
      it.name .. "' first.", vim.log.levels.INFO)
    return
  end
  -- Finished active: run simulate. `session.simulate` opens its own window,
  -- so close the picker first.
  local alias = it.name
  close()
  vim.schedule(function() session.simulate(alias) end)
end

local function act_delete()
  local it = current_item()
  if not it then return end
  if it.is_synthetic then
    vim.notify("Recall ring cannot be deleted from the picker.", vim.log.levels.INFO)
    return
  end
  local prompt
  if it.pane == "active" then
    prompt = "Close session [" .. it.name .. "]? (y/N): "
  else
    prompt = "Delete saved [" .. it.name .. "]? (y/N): "
  end
  vim.ui.input({ prompt = prompt }, function(input)
    if not input or input:lower() ~= "y" then return end
    if it.pane == "active" then
      session_store.remove(it.key)
    else
      session.rm(it.name)
    end
    state.marked[mark_key(it)] = nil
    render()
  end)
end

local function act_delete_marked()
  local keys = {}
  for k, _ in pairs(state.marked) do table.insert(keys, k) end
  if #keys == 0 then
    vim.notify("No marked entries.", vim.log.levels.INFO)
    return
  end
  vim.ui.input({ prompt = "Delete " .. #keys .. " marked? (y/N): " }, function(input)
    if not input or input:lower() ~= "y" then return end
    -- Split keys into active vs saved via the pane prefix.
    for _, mk in ipairs(keys) do
      local pane, rest = mk:match("^(%a+):(.+)$")
      if pane == "active" then
        session_store.remove(rest)
      elseif pane == "saved" then
        session.rm(rest)
      end
    end
    state.marked = {}
    render()
  end)
end

--- True iff this item is still accumulating keystrokes (ongoing), so actions
--- that mutate its identity (rename/duplicate/open) should be refused.
local function is_ongoing(it)
  return it.is_synthetic
      or (it.pane == "active" and it.status == "active")
end

--- Finished active-pane entries are renameable/duplicable only when they carry
--- a stable manual alias. Recall/suggest entries drift (their display alias is
--- a ring index), so the picker points the user at save-to-disk instead.
local function has_stable_alias(it)
  return it.pane == "saved" or alias_mod.is_valid_manual(it.name)
end

local function notify_needs_save_first(it, verb)
  vim.notify(string.format(
    "Session '%s' has no stable alias to %s. Save it first " ..
    "(`:Vimfy save %s <name>`), then %s from the Saved pane.",
    it.name, verb, it.name, verb), vim.log.levels.INFO)
end

local function act_rename()
  local it = current_item()
  if not it then return end
  if is_ongoing(it) then
    vim.notify("Can't rename an in-progress session — finish it first.",
      vim.log.levels.INFO)
    return
  end
  if not has_stable_alias(it) then
    notify_needs_save_first(it, "rename")
    return
  end
  vim.ui.input({ prompt = "Rename " .. it.name .. " -> ", default = it.name }, function(input)
    if not input or input == "" or input == it.name then return end
    local ok, err
    if it.pane == "saved" then
      ok, err = session.rename_saved(it.name, input)
    else
      ok, err = session.rename_active(it.name, input)
    end
    if not ok then
      vim.notify(err or "rename failed", vim.log.levels.ERROR)
      return
    end
    vim.notify("renamed " .. it.name .. " -> " .. input, vim.log.levels.INFO)
    render()
  end)
end

local function act_duplicate()
  local it = current_item()
  if not it then return end
  if is_ongoing(it) then
    vim.notify("Can't duplicate an in-progress session — finish it first.",
      vim.log.levels.INFO)
    return
  end
  if not has_stable_alias(it) then
    notify_needs_save_first(it, "duplicate")
    return
  end
  local default = it.pane == "saved" and (it.name .. "_copy") or ""
  vim.ui.input({ prompt = "Duplicate " .. it.name .. " -> ", default = default },
    function(input)
      if not input or input == "" then return end
      local ok, err
      if it.pane == "saved" then
        ok, err = session.duplicate_saved(it.name, input)
      else
        ok, err = session.duplicate_active(it.name, input)
      end
      if not ok then
        vim.notify(err or "duplicate failed", vim.log.levels.ERROR)
        return
      end
      vim.notify("duplicated " .. it.name .. " -> " .. input, vim.log.levels.INFO)
      render()
    end)
end

local HELP_LINES = {
  "  Vimficiency session picker",
  "",
  "  /          fuzzy search",
  "  <Tab>      switch Active ↔ Saved pane",
  "  sn / sN    sort by name      (A→Z / Z→A)",
  "  sc / sC    sort by category  (A→Z / Z→A)",
  "  st / sT    sort by time      (newest / oldest)",
  "  s          sort menu (s? shows the hint popup)",
  "  <CR>       open",
  "  d          delete",
  "  m          toggle mark",
  "  D          delete marked",
  "  r          rename",
  "  y          duplicate",
  "  ?          this help",
  "  q / <Esc>  close",
}

local function act_help()
  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  v.nvim_buf_set_lines(buf, 0, -1, false, HELP_LINES)
  vim.bo[buf].modifiable = false
  local width = 56
  local height = #HELP_LINES + 2
  local row = math.floor((vim.o.lines - height) / 2)
  local col = math.floor((vim.o.columns - width) / 2)
  local win = v.nvim_open_win(buf, true, {
    relative = "editor", row = row, col = col,
    width = width, height = height, border = "rounded", style = "minimal",
  })
  vim.keymap.set("n", "q",     function() pcall(v.nvim_win_close, win, true) end, { buffer = buf, nowait = true })
  vim.keymap.set("n", "<Esc>", function() pcall(v.nvim_win_close, win, true) end, { buffer = buf, nowait = true })
  vim.keymap.set("n", "?",     function() pcall(v.nvim_win_close, win, true) end, { buffer = buf, nowait = true })
end

--------------------------------------------------------------------------------
-- Window construction
--------------------------------------------------------------------------------

local function install_keymaps(buf)
  local function map(lhs, fn)
    vim.keymap.set("n", lhs, fn, { buffer = buf, nowait = true, silent = true })
  end
  map("q",       close)
  map("<Esc>",   close)
  map("<Tab>",   act_toggle_pane)
  map("sn",      function() act_sort_apply("alpha",    false) end)
  map("sN",      function() act_sort_apply("alpha",    true)  end)
  map("sc",      function() act_sort_apply("category", false) end)
  map("sC",      function() act_sort_apply("category", true)  end)
  map("st",      function() act_sort_apply("created",  false) end)
  map("sT",      function() act_sort_apply("created",  true)  end)
  map("s?",      act_sort_popup)
  map("s",       act_sort_popup)
  map("/",       act_search)
  map("<CR>",    act_open)
  map("d",       act_delete)
  map("D",       act_delete_marked)
  map("m",       act_mark)
  map("r",       act_rename)
  map("y",       act_duplicate)
  map("?",       act_help)
end

function M.open()
  if state then
    -- Already open: focus it.
    pcall(v.nvim_set_current_win, state.win)
    return
  end

  local total_cols = vim.o.columns
  local total_rows = vim.o.lines
  local width  = math.floor(total_cols * 0.8)
  local height = math.floor(total_rows * 0.75)
  local row    = math.floor((total_rows - height) / 2)
  local col    = math.floor((total_cols - width) / 2)

  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency_picker"

  local win = v.nvim_open_win(buf, true, {
    relative = "editor",
    row = row, col = col,
    width = width, height = height,
    border = "rounded", style = "minimal",
    title = " Vimficiency Sessions ", title_pos = "center",
  })
  util.configure_scratch_window(win, { cursorline = true })

  -- Prompt split (bottom, 1 line, full float width).
  vim.cmd("belowright split")
  local prompt_win = v.nvim_get_current_win()
  local prompt_buf = v.nvim_create_buf(false, true)
  vim.bo[prompt_buf].buftype = "nofile"
  vim.bo[prompt_buf].bufhidden = "wipe"
  vim.bo[prompt_buf].swapfile = false
  vim.bo[prompt_buf].filetype = "vimficiency_picker_prompt"
  v.nvim_win_set_buf(prompt_win, prompt_buf)
  v.nvim_win_set_height(prompt_win, 1)
  util.configure_scratch_window(prompt_win, {
    winfixheight = true,
    statusline   = "/",
  })

  -- Back to list (top) and split off the preview pane.
  v.nvim_set_current_win(win)
  vim.cmd("belowright vsplit")
  local prev_win = v.nvim_get_current_win()
  local prev_buf = v.nvim_create_buf(false, true)
  vim.bo[prev_buf].buftype = "nofile"
  vim.bo[prev_buf].bufhidden = "wipe"
  vim.bo[prev_buf].swapfile = false
  vim.bo[prev_buf].filetype = "vimficiency_picker_preview"
  v.nvim_win_set_buf(prev_win, prev_buf)
  util.configure_scratch_window(prev_win)
  local prev_width = math.floor(width * 0.4)
  pcall(v.nvim_win_set_width, prev_win, prev_width)

  v.nvim_set_current_win(win)

  state = {
    win = win, buf = buf,
    prev_win = prev_win, prev_buf = prev_buf,
    prompt_win = prompt_win, prompt_buf = prompt_buf,
    pane = "active",
    sort_mode = "category",
    direction = DEFAULT_DIRECTIONS.category,
    query = "",
    marked = {},
    rows = {},
    cursor = 1,
  }

  install_keymaps(buf)

  -- Prompt bindings: CR picks, Esc/q returns to list, C-n/C-p + arrows move list.
  local function pmap(mode, lhs, fn)
    vim.keymap.set(mode, lhs, fn,
      { buffer = prompt_buf, nowait = true, silent = true })
  end
  pmap("i", "<CR>",   function() exit_prompt_to_list(); act_open() end)
  pmap("i", "<Esc>",  exit_prompt_to_list)
  pmap("i", "<C-n>",  function() move_list(1) end)
  pmap("i", "<C-p>",  function() move_list(-1) end)
  pmap("i", "<Down>", function() move_list(1) end)
  pmap("i", "<Up>",   function() move_list(-1) end)
  pmap("n", "<CR>",   function() exit_prompt_to_list(); act_open() end)
  pmap("n", "<Esc>",  exit_prompt_to_list)
  pmap("n", "q",      exit_prompt_to_list)

  -- Live-filter on every prompt-buffer change (insert or normal mode edits).
  v.nvim_create_autocmd({ "TextChangedI", "TextChanged" }, {
    buffer = prompt_buf,
    callback = function()
      if not state then return end
      local line = v.nvim_buf_get_lines(prompt_buf, 0, 1, false)[1] or ""
      if line ~= state.query then
        state.query = line
        render()
      end
    end,
  })

  -- Update preview as the user moves the cursor in the list.
  v.nvim_create_autocmd("CursorMoved", {
    buffer = buf,
    callback = function()
      if not state then return end
      local row_now = v.nvim_win_get_cursor(state.win)[1]
      state.cursor = row_now
      M.update_preview()
    end,
  })

  -- Close cleanly if any of the picker windows is closed by other means.
  v.nvim_create_autocmd("WinClosed", {
    pattern = { tostring(win), tostring(prev_win), tostring(prompt_win) },
    once = true,
    callback = function() close() end,
  })

  render()
end

return M
