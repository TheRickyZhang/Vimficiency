-- Interactive settings modal for the explore session.
--
-- Generic-ish: the module knows how to render a list of typed settings
-- (bool / int / enum), navigate rows, and adjust the selected row. It
-- does NOT know about `active` — the caller builds the schema by binding
-- get/set closures to whatever backing store fits.
--
-- Schema entry shape:
--   { label = "Display mode",
--     type  = "bool" | "int" | "enum",
--     get   = function() return current_value end,
--     set   = function(new_value) ... end,
--     -- `int` only:
--     min = 0, max = 10,
--     -- `enum` only:
--     values = { "off", "highlight", ... } }
--
-- Keybinds inside the modal:
--   j / <Down>   move selection down
--   k / <Up>     move selection up
--   <Space> / <Enter> / <Tab> / <Right> / l / +
--                forward-adjust (toggle bool / next enum / +1 int)
--   <S-Tab> / <Left> / h / -
--                backward-adjust (toggle bool / prev enum / −1 int)
--   q / <Esc>    close
--   ?            show help footer again (rerenders the window)
local util = require("vimficiency.util")

local M = {}

local v = vim.api

local FOOTER_LINES = {
  "",
  "j/k  move       <Space>/<Enter>/+  forward       -    backward",
  "                <Tab>/<Right>/l    enum/int ↑   <S-Tab>/<Left>/h  enum/int ↓",
  "q / <Esc>  close           ?  show this help",
}

---Apply `delta ∈ {+1, -1}` to the given entry, mutating via its `set`.
---Bool: toggled regardless of sign. Int: clamped to [min, max]. Enum:
---cycled modulo #values.
---@param entry table
---@param delta integer
local function adjust(entry, delta)
  if entry.type == "bool" then
    entry.set(not entry.get())
  elseif entry.type == "int" then
    local cur = entry.get()
    local next_val = cur + delta
    if entry.min ~= nil and next_val < entry.min then next_val = entry.min end
    if entry.max ~= nil and next_val > entry.max then next_val = entry.max end
    if next_val ~= cur then entry.set(next_val) end
  elseif entry.type == "enum" then
    local values = entry.values
    local cur = entry.get()
    local idx = 1
    for i, v_ in ipairs(values) do if v_ == cur then idx = i; break end end
    local n = #values
    local next_idx = ((idx - 1 + delta) % n + n) % n + 1
    entry.set(values[next_idx])
  end
end

---Format an entry's current value for the right-hand display column.
---@param entry table
---@return string
local function format_value(entry)
  local v_ = entry.get()
  if entry.type == "bool" then
    return v_ and "[on]" or "[off]"
  elseif entry.type == "int" then
    return string.format("[%d]", v_)
  elseif entry.type == "enum" then
    return string.format("[%s]", tostring(v_))
  end
  return "[?]"
end

---Render the modal's lines from the schema + footer.
---@param schema table[]
---@return string[]
local function build_lines(schema)
  -- Left-column width: longest label.
  local label_w = 0
  for _, entry in ipairs(schema) do
    label_w = math.max(label_w, #entry.label)
  end
  local lines = {}
  lines[#lines + 1] = "Vimficiency Explore — Settings"
  lines[#lines + 1] = ""
  for _, entry in ipairs(schema) do
    lines[#lines + 1] = string.format("  %-" .. label_w .. "s   %s", entry.label, format_value(entry))
  end
  for _, s in ipairs(FOOTER_LINES) do lines[#lines + 1] = s end
  return lines
end

---Open the settings modal. Re-renders on each adjustment; calls `on_change`
---after every mutation so the caller can refresh its own UI.
---@param schema table[]
---@param on_change fun()|nil
function M.open(schema, on_change)
  if #schema == 0 then return end

  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency"

  -- Row index of the first setting in the buffer (0-indexed). Matches the
  -- two header lines ("Settings" + blank) above the schema rows.
  local HEADER_ROWS = 2
  local selection = 1  -- 1-indexed schema row

  local function render()
    local lines = build_lines(schema)
    vim.bo[buf].modifiable = true
    v.nvim_buf_set_lines(buf, 0, -1, false, lines)
    vim.bo[buf].modifiable = false
  end

  local function clamp_selection()
    if selection < 1 then selection = 1 end
    if selection > #schema then selection = #schema end
  end

  local function place_cursor(win)
    clamp_selection()
    pcall(v.nvim_win_set_cursor, win, { HEADER_ROWS + selection, 2 })
  end

  render()

  local lines = build_lines(schema)
  local width = 0
  for _, line in ipairs(lines) do
    width = math.max(width, vim.fn.strdisplaywidth(line))
  end

  local ui = v.nvim_list_uis()[1]
  local win_width = math.min(width + 4, math.max(40, ui.width - 4))
  local win_height = math.min(#lines, math.max(8, ui.height - 4))
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
    title = " Explore Settings ",
    title_pos = "center",
    noautocmd = true,
  })
  vim.wo[win].wrap = false
  vim.wo[win].cursorline = true
  vim.wo[win].number = false
  vim.wo[win].relativenumber = false
  vim.wo[win].signcolumn = "no"
  place_cursor(win)

  local function close_popup()
    if v.nvim_win_is_valid(win) then v.nvim_win_close(win, true) end
  end

  local function move(step)
    selection = selection + step
    place_cursor(win)
  end

  local function adjust_selection(delta)
    clamp_selection()
    adjust(schema[selection], delta)
    render()
    place_cursor(win)
    if on_change then on_change() end
  end

  util.set_buffer_keymaps(buf, {
    { lhs = "j",       handler = function() move(1) end,               desc = "Next setting",         nowait = true },
    { lhs = "<Down>",  handler = function() move(1) end,               desc = "Next setting",         nowait = true },
    { lhs = "k",       handler = function() move(-1) end,              desc = "Previous setting",     nowait = true },
    { lhs = "<Up>",    handler = function() move(-1) end,              desc = "Previous setting",     nowait = true },

    { lhs = "<Space>", handler = function() adjust_selection(1) end,   desc = "Forward-adjust",       nowait = true },
    { lhs = "<CR>",    handler = function() adjust_selection(1) end,   desc = "Forward-adjust",       nowait = true },
    { lhs = "<Tab>",   handler = function() adjust_selection(1) end,   desc = "Forward-adjust",       nowait = true },
    { lhs = "<Right>", handler = function() adjust_selection(1) end,   desc = "Forward-adjust",       nowait = true },
    { lhs = "l",       handler = function() adjust_selection(1) end,   desc = "Forward-adjust",       nowait = true },
    { lhs = "+",       handler = function() adjust_selection(1) end,   desc = "Forward-adjust",       nowait = true },

    { lhs = "<S-Tab>", handler = function() adjust_selection(-1) end,  desc = "Backward-adjust",      nowait = true },
    { lhs = "<Left>",  handler = function() adjust_selection(-1) end,  desc = "Backward-adjust",      nowait = true },
    { lhs = "h",       handler = function() adjust_selection(-1) end,  desc = "Backward-adjust",      nowait = true },
    { lhs = "-",       handler = function() adjust_selection(-1) end,  desc = "Backward-adjust",      nowait = true },

    { lhs = "q",       handler = close_popup,                          desc = "Close settings",       nowait = true },
    { lhs = "<Esc>",   handler = close_popup,                          desc = "Close settings",       nowait = true },
  })
end

return M
