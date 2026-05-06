-- Shared settings widget for Vimfy.
--
-- Two presentations use it: the explore `gs` side panel (a botright
-- vsplit) and the play `gs` modal (a centered float). Callers create
-- their own buffer + window in whichever shape they want, then call
-- `M.attach(buf, win, opts)`. The widget owns rendering, allow-list-only
-- keymaps (j/k/Down/Up navigate · Tab/Right/S-Tab/Left step ·
-- CR/i smart-input · q/Esc/gs close), cursor hiding, and the close
-- lifecycle.
--
-- Design notes:
--   - Strict-mode keymaps are the only restriction primitive used. Any
--     key not in the allow list is bound to <Nop>, so cursor / cmdline
--     / search / mode-change side effects can't leak. No CursorMoved
--     snapping or vim.on_key trickery — by construction, the cursor
--     can't move because nothing input-side moves it.
--   - The cursor cell is hidden via a temporary `guicursor` swap on
--     BufEnter / WinEnter, restored on BufLeave / WinLeave / BufWipeout.
--     Combined with the `blend = 100` highlight on
--     `VimficiencySettingsCursorHidden`, modern TUIs render no cursor;
--     older ones fall back to the row-selection extmark for state.
--   - The buffer is non-modifiable, so there's no edit path either.
local highlights = require("vimficiency.highlights")

local M = {}
local v = vim.api

local selection_ns = v.nvim_create_namespace("vimficiency_settings_selection")
local expansion_ns = v.nvim_create_namespace("vimficiency_settings_expansion")

-- Soft cap on `kind = "setting"` labels. Picked to fit "Directional pruning"
-- (the longest current label) so the value column stays close enough to the
-- label that the inline range hint has room on the right in a 40-col panel.
-- Action labels are exempt — they render alone on their row.
local MAX_LABEL_LENGTH = 19

-- =============================================================================
-- Row primitives
-- =============================================================================

---@param row table
---@return boolean
function M.is_selectable(row)
  return row.kind == "setting" or row.kind == "action"
end

---@param rows table[]
---@return integer?
function M.first_selectable_row(rows)
  for i, row in ipairs(rows) do
    if M.is_selectable(row) then return i end
  end
  return nil
end

---@param rows table[]
---@param current integer
---@param step integer
---@return integer
function M.next_selectable_row(rows, current, step)
  local i = current + step
  while i >= 1 and i <= #rows do
    if M.is_selectable(rows[i]) then return i end
    i = i + step
  end
  return current
end

---@param rows table[]
---@param selection integer
---@return integer?
function M.clamp_selection(rows, selection)
  if #rows == 0 then return nil end
  selection = math.max(1, math.min(selection, #rows))
  if M.is_selectable(rows[selection]) then return selection end
  local up = M.next_selectable_row(rows, selection, -1)
  if up ~= selection then return up end
  local down = M.next_selectable_row(rows, selection, 1)
  if down ~= selection then return down end
  return M.first_selectable_row(rows)
end

---Apply `delta` to the given row. Action rows run only on forward adjust.
---@param row table
---@param delta integer
function M.adjust(row, delta)
  if row.kind == "action" then
    if delta > 0 then row.run() end
    return
  end
  if row.value_kind == "bool" then
    row.set(not row.get())
  elseif row.value_kind == "int" then
    local cur = row.get()
    local step = row.step or 1
    local next_val = cur + delta * step
    if row.min ~= nil and next_val < row.min then next_val = row.min end
    if row.max ~= nil and next_val > row.max then next_val = row.max end
    if next_val ~= cur then row.set(next_val) end
  elseif row.value_kind == "float" then
    local cur = row.get()
    local step = row.step or 0.1
    local next_val = cur + delta * step
    if row.min ~= nil and next_val < row.min then next_val = row.min end
    if row.max ~= nil and next_val > row.max then next_val = row.max end
    -- Snap to 3 decimal places to avoid 1.0000000004 from repeated 0.1 adds.
    next_val = math.floor(next_val * 1000 + 0.5) / 1000
    if next_val ~= cur then row.set(next_val) end
  elseif row.value_kind == "enum" then
    local values = row.values
    local cur = row.get()
    local idx = 1
    for i, val in ipairs(values) do if val == cur then idx = i; break end end
    local n = #values
    local next_idx = ((idx - 1 + delta) % n + n) % n + 1
    row.set(values[next_idx])
  end
end

---@param row table
---@return string
function M.format_value(row)
  if row.kind ~= "setting" then return "" end
  if row.value_kind == "bool" then return row.get() and "[✓]" or "[ ]" end
  if row.value_kind == "int" then return string.format("[%d]", row.get()) end
  if row.value_kind == "float" then
    local fmt = row.format or "[%.2f]"
    return string.format(fmt, row.get())
  end
  if row.value_kind == "enum" then return "[" .. tostring(row.get()) .. "]" end
  return "?"
end

---@param rows table[]
---@return integer
local function label_width(rows)
  local w = 0
  for _, row in ipairs(rows) do
    if row.kind == "setting" then w = math.max(w, #row.label) end
  end
  return w
end

---@param rows table[]
---@param opts? { row_prefix?: string, value_gap?: string }
---@return { lines: string[], line_for_row: integer[], label_w: integer, value_col: integer }
function M.build_layout(rows, opts)
  opts = opts or {}
  local row_prefix = opts.row_prefix or "  "
  local value_gap = opts.value_gap or "   "
  local label_w = label_width(rows)
  local lines = {}
  local line_for_row = {}

  for i, row in ipairs(rows) do
    line_for_row[i] = #lines + 1
    if row.kind == "separator" then
      lines[#lines + 1] = ""
    elseif row.kind == "action" then
      lines[#lines + 1] = row_prefix .. row.label
    elseif row.kind == "hint" then
      lines[#lines + 1] = row_prefix .. row.text
    else
      lines[#lines + 1] = string.format("%s%-" .. label_w .. "s%s%s",
        row_prefix, row.label, value_gap, M.format_value(row))
    end
  end

  return {
    lines = lines,
    line_for_row = line_for_row,
    label_w = label_w,
    value_col = #row_prefix + label_w + #value_gap,
  }
end

---@param row table
---@param on_apply fun(value: number)
---@return boolean prompted
function M.prompt_numeric(row, on_apply)
  if row.kind ~= "setting" then return false end
  if row.value_kind ~= "int" and row.value_kind ~= "float" then return false end

  vim.ui.input({
    prompt = row.label .. " = ",
    default = tostring(row.get()),
  }, function(input)
    if input == nil or input == "" then return end
    local value = tonumber(input)
    if value == nil then
      vim.notify("vimfy settings: not a number: " .. input, vim.log.levels.WARN)
      return
    end
    if row.value_kind == "int" then value = math.floor(value) end
    if row.min ~= nil and value < row.min then value = row.min end
    if row.max ~= nil and value > row.max then value = row.max end
    row.set(value)
    on_apply(value)
  end)

  return true
end

-- =============================================================================
-- Strict-mode keymap installer
-- =============================================================================

-- Generated once at module load. Anything in this list that isn't in the
-- allow-list passed to `attach` becomes a `<Nop>` mapping for the buffer.
-- Multi-key allow-list entries (`gs`, `g?`) work because their prefix
-- denial (`g` → `<Nop>`) is registered without `nowait`, so Neovim still
-- waits for the next character.
local function build_default_deny_list()
  local denied = {}
  local function add(k) denied[#denied + 1] = k end

  for byte = string.byte("a"), string.byte("z") do add(string.char(byte)) end
  for byte = string.byte("A"), string.byte("Z") do add(string.char(byte)) end
  for byte = string.byte("0"), string.byte("9") do add(string.char(byte)) end
  for _, p in ipairs({
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")",
    "_", "+", "-", "=", "[", "]", "{", "}", "|", ";",
    ":", "'", '"', ",", ".", "/", "<", ">", "?", "~", "`",
  }) do add(p) end
  for _, k in ipairs({
    "<Up>", "<Down>", "<Left>", "<Right>",
    "<Home>", "<End>", "<PageUp>", "<PageDown>",
    "<Insert>", "<Del>", "<BS>", "<CR>", "<Esc>", "<Tab>", "<S-Tab>", "<Space>",
  }) do add(k) end
  for byte = string.byte("a"), string.byte("z") do
    add("<C-" .. string.char(byte) .. ">")
  end
  return denied
end

local DEFAULT_DENY_LIST = build_default_deny_list()

---Map every key in `DEFAULT_DENY_LIST` not present in `allowed_lhs` to
---<Nop> on `buf`. The caller's explicit allow-list keymaps must already
---be installed; this just locks down everything else.
---@param buf integer
---@param allowed_lhs string[]
local function install_strict_mode(buf, allowed_lhs)
  local allowed = {}
  for _, lhs in ipairs(allowed_lhs) do allowed[lhs] = true end

  for _, lhs in ipairs(DEFAULT_DENY_LIST) do
    if not allowed[lhs] then
      -- nowait is left default (false) so prefix keys like `g` still wait
      -- for a possible longer allow-list match (`gs`, `g?`).
      vim.keymap.set("n", lhs, "<Nop>", { buffer = buf, silent = true })
    end
  end
end

-- =============================================================================
-- Cursor hiding
-- =============================================================================

---Swap `guicursor` to a `blend = 100` highlight while `buf` is active.
---Returns a `cleanup()` callback that restores the saved guicursor and
---tears down the autocmds. The widget calls cleanup from its `close`
---path; BufWipeout also fires it as a safety net.
---@param win integer
---@param buf integer
---@return fun()
local function hide_cursor_while_active(win, buf)
  local saved
  local closed = false
  local group = v.nvim_create_augroup(
    "vimficiency_settings_cursor_" .. buf, { clear = true })

  local function active_here()
    return not closed
      and v.nvim_win_is_valid(win)
      and v.nvim_get_current_win() == win
      and v.nvim_win_get_buf(win) == buf
  end

  local function activate()
    if closed or saved then return end
    saved = vim.go.guicursor
    vim.go.guicursor = "a:" .. highlights.SETTINGS_CURSOR_HIDDEN
  end

  local function deactivate()
    if not saved then return end
    vim.go.guicursor = saved
    saved = nil
  end

  local function cleanup()
    if closed then return end
    closed = true
    deactivate()
    pcall(v.nvim_del_augroup_by_id, group)
  end

  v.nvim_create_autocmd({ "BufEnter", "WinEnter" }, {
    group = group,
    buffer = buf,
    callback = function()
      if active_here() then activate() end
    end,
    desc = "vimfy settings: hide cursor",
  })
  v.nvim_create_autocmd({ "BufLeave", "WinLeave" }, {
    group = group,
    buffer = buf,
    callback = deactivate,
    desc = "vimfy settings: restore cursor",
  })
  v.nvim_create_autocmd("BufWipeout", {
    group = group,
    buffer = buf,
    callback = cleanup,
    desc = "vimfy settings: clean cursor hooks",
  })

  if active_here() then activate() end
  return cleanup
end

-- =============================================================================
-- Render
-- =============================================================================

---@param state VF.SettingsWidget
local function render_lines(state)
  state.layout = M.build_layout(state.rows, state.layout_opts)
  vim.bo[state.buf].modifiable = true
  v.nvim_buf_set_lines(state.buf, 0, -1, false, state.layout.lines)
  vim.bo[state.buf].modifiable = false
end

---@param state VF.SettingsWidget
local function render_selection(state)
  v.nvim_buf_clear_namespace(state.buf, selection_ns, 0, -1)
  state.selection = M.clamp_selection(state.rows, state.selection)
  if not state.selection then return end
  local row = state.rows[state.selection]
  if not row then return end

  local line_idx = state.layout.line_for_row[state.selection] - 1
  local line_text = state.layout.lines[line_idx + 1] or ""
  v.nvim_buf_set_extmark(state.buf, selection_ns, line_idx, 0, {
    end_row = line_idx,
    end_col = #line_text,
    hl_group = "CursorLine",
    hl_eol = true,
    priority = 50,
  })
  if row.kind == "action" then
    v.nvim_buf_set_extmark(state.buf, selection_ns, line_idx, 0, {
      end_row = line_idx,
      end_col = #line_text,
      hl_group = highlights.SETTINGS_ACTION_ACTIVE,
      priority = 2100,
    })
  end
end

---Build wrapped enum chunks. Options pack onto a line until they exceed
---`max_width`, then start a new line. Lines are unindented (col 0).
---@param row table
---@param max_width integer
---@return table[]
local function build_enum_virt_lines(row, max_width)
  local current = row.get()
  local virt_lines = {}
  local chunks = {}
  local used = 0
  for i, option in ipairs(row.values) do
    local sep = (i > 1 and used > 0) and "  " or ""
    local option_width = vim.fn.strdisplaywidth(option)
    if used > 0 and used + #sep + option_width > max_width then
      virt_lines[#virt_lines + 1] = chunks
      chunks = {}
      used = 0
      sep = ""
    end
    if sep ~= "" then
      chunks[#chunks + 1] = { sep, "Normal" }
      used = used + #sep
    end
    local hl = (option == current) and "Title" or "Comment"
    chunks[#chunks + 1] = { option, hl }
    used = used + option_width
  end
  virt_lines[#virt_lines + 1] = chunks
  return virt_lines
end

---Format the inline `min..max` hint for a numeric row. Floats use `%.1f`
---rather than the value column's `%.2f` to keep the hint narrow enough
---to fit alongside the value in a 40-col panel; one decimal is still
---enough to convey magnitude. Includes the leading separator. Exposed
---so the modal can include it in its width calculation.
---@param row table
---@return string
function M.format_range_hint(row)
  local fmt = (row.value_kind == "float") and "%.1f" or "%d"
  return string.format("  " .. fmt .. ".." .. fmt, row.min, row.max)
end

---Width of the unindented enum line (sum of options + 2-space separators).
---Exposed so the modal can size itself to fit the expansion.
---@param row table
---@return integer
function M.enum_line_width(row)
  if row.value_kind ~= "enum" or not row.values then return 0 end
  local w = 0
  for i, option in ipairs(row.values) do
    if i > 1 then w = w + 2 end
    w = w + vim.fn.strdisplaywidth(option)
  end
  return w
end

---Render the per-kind hint for the selected row:
---  - int/float with min/max: inline `virt_text` after the value
---  - enum: unindented `virt_lines` below the row, wrapping if needed
---  - bool/action: nothing
---@param state VF.SettingsWidget
local function render_expansion(state)
  v.nvim_buf_clear_namespace(state.buf, expansion_ns, 0, -1)
  if not state.selection then return end
  local row = state.rows[state.selection]
  if not row or row.kind ~= "setting" then return end

  local line_idx = state.layout.line_for_row[state.selection] - 1

  if (row.value_kind == "int" or row.value_kind == "float")
      and row.min ~= nil and row.max ~= nil then
    v.nvim_buf_set_extmark(state.buf, expansion_ns, line_idx, 0, {
      virt_text = { { M.format_range_hint(row), "Comment" } },
      virt_text_pos = "eol",
    })
    return
  end

  if row.value_kind == "enum" and row.values then
    local max_width = math.max(8, v.nvim_win_get_width(state.win) - 2)
    v.nvim_buf_set_extmark(state.buf, expansion_ns, line_idx, 0, {
      virt_lines = build_enum_virt_lines(row, max_width),
      virt_lines_above = false,
    })
  end
end

---@param state VF.SettingsWidget
local function pin_cursor_to_selection(state)
  if not state.selection then return end
  if not v.nvim_win_is_valid(state.win) then return end
  local sel_line = state.layout.line_for_row[state.selection]
  if sel_line then pcall(v.nvim_win_set_cursor, state.win, { sel_line, 0 }) end
end

---@param state VF.SettingsWidget
local function full_render(state)
  render_lines(state)
  render_selection(state)
  render_expansion(state)
  pin_cursor_to_selection(state)
end

-- =============================================================================
-- attach
-- =============================================================================

---@class VF.SettingsWidget.Opts
---@field rows table[]
---@field on_change fun()|nil   -- fired after a successful adjust / run / prompt
---@field on_close fun()|nil    -- fired when the widget closes (including externally)
---@field row_prefix string|nil
---@field value_gap string|nil

---@class VF.SettingsWidget
---@field buf integer
---@field win integer
---@field rows table[]
---@field selection integer|nil
---@field layout table
---@field layout_opts { row_prefix?: string, value_gap?: string }
---@field on_change fun()|nil
---@field on_close fun()|nil
---@field cleanup_cursor fun()|nil
---@field closed boolean

---@class VF.SettingsWidget.Handle
---@field refresh fun()                       -- re-render with current rows (call after external mutations)
---@field set_rows fun(rows: table[])         -- replace the schema and re-render
---@field close fun()                         -- closes the window and runs on_close
---@field is_open fun(): boolean
---@field selection fun(): integer|nil

---Attach the settings widget to `buf` rendered in `win`. Caller created
---the buffer/window in whichever shape they want (split or float); this
---owns everything else.
---@param buf integer
---@param win integer
---@param opts VF.SettingsWidget.Opts
---@return VF.SettingsWidget.Handle
function M.attach(buf, win, opts)
  for _, row in ipairs(opts.rows) do
    if row.kind == "setting" then
      assert(#row.label <= MAX_LABEL_LENGTH, string.format(
        "vimfy settings: label %q is %d chars (cap %d)",
        row.label, #row.label, MAX_LABEL_LENGTH))
    end
  end

  local state = {
    buf = buf,
    win = win,
    rows = opts.rows,
    selection = M.first_selectable_row(opts.rows),
    layout = nil,
    layout_opts = {
      row_prefix = opts.row_prefix,
      value_gap = opts.value_gap,
    },
    on_change = opts.on_change,
    on_close = opts.on_close,
    closed = false,
  }
  assert(state.selection, "vimfy settings: schema has no selectable rows")

  highlights.refresh()

  local handle = {}

  local function close()
    if state.closed then return end
    state.closed = true
    if state.cleanup_cursor then state.cleanup_cursor() end
    if v.nvim_win_is_valid(state.win) then
      v.nvim_win_close(state.win, true)
    end
    if state.on_close then state.on_close() end
  end

  local function move(step)
    state.selection = M.next_selectable_row(state.rows, state.selection or 1, step)
    full_render(state)
  end

  local function adjust(delta)
    if not state.selection then return end
    M.adjust(state.rows[state.selection], delta)
    full_render(state)
    if state.on_change then state.on_change() end
  end

  ---`i` / `<CR>` is the "smart input" key: open a numeric prompt for
  ---int/float rows; for everything else, fall through to the same
  ---forward step as Tab/Right (toggle bool, cycle enum next, run action).
  local function input_smart()
    if not state.selection then return end
    local row = state.rows[state.selection]
    if row.kind == "setting"
        and (row.value_kind == "int" or row.value_kind == "float") then
      M.prompt_numeric(row, function()
        full_render(state)
        if state.on_change then state.on_change() end
      end)
      return
    end
    M.adjust(row, 1)
    full_render(state)
    if state.on_change then state.on_change() end
  end

  local function map(lhs, fn)
    vim.keymap.set("n", lhs, fn, { buffer = state.buf, nowait = true, silent = true })
  end
  -- Vertical navigation: j / k / arrows are aliases.
  map("j",       function() move(1) end)
  map("<Down>",  function() move(1) end)
  map("k",       function() move(-1) end)
  map("<Up>",    function() move(-1) end)
  -- Horizontal step: Tab/Right and S-Tab/Left are aliases.
  map("<Tab>",   function() adjust(1) end)
  map("<Right>", function() adjust(1) end)
  map("<S-Tab>", function() adjust(-1) end)
  map("<Left>",  function() adjust(-1) end)
  -- Smart input: <CR> / i.
  map("<CR>",    input_smart)
  map("i",       input_smart)
  -- Close.
  map("q",       close)
  map("<Esc>",   close)
  map("gs",      close)

  install_strict_mode(state.buf, {
    "j", "k", "<Down>", "<Up>",
    "<Tab>", "<S-Tab>", "<Right>", "<Left>",
    "<CR>", "i",
    "q", "<Esc>",
    -- Prefix `g` is not in the allow list but its `<Nop>` mapping has no
    -- nowait, so `gs` (multi-key) wins via Neovim's longest-match wait.
  })

  state.cleanup_cursor = hide_cursor_while_active(state.win, state.buf)
  full_render(state)

  function handle.refresh() if not state.closed then full_render(state) end end
  function handle.set_rows(new_rows)
    if state.closed then return end
    state.rows = new_rows
    state.selection = M.clamp_selection(new_rows, state.selection or 1)
      or M.first_selectable_row(new_rows)
    full_render(state)
  end
  function handle.close() close() end
  function handle.is_open() return not state.closed and v.nvim_win_is_valid(state.win) end
  function handle.selection() return state.selection end

  return handle
end

return M
