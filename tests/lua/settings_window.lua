local helpers = require("_helpers")
local settings_window = require("vimficiency.settings_window")

local selection_ns = vim.api.nvim_create_namespace("vimficiency_settings_selection")

local function open_widget(rows, opts)
  local buf = vim.api.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false

  local win = vim.api.nvim_open_win(buf, true, {
    relative = "editor",
    row = 1,
    col = 1,
    width = 50,
    height = 8,
    style = "minimal",
  })

  local attach_opts = vim.tbl_extend("force", opts or {}, { rows = rows })
  local handle = settings_window.attach(buf, win, attach_opts)
  return { buf = buf, win = win, handle = handle }
end

local function close_widget(widget)
  if not widget then return end
  if widget.handle then pcall(widget.handle.close) end
  if widget.win and vim.api.nvim_win_is_valid(widget.win) then
    pcall(vim.api.nvim_win_close, widget.win, true)
  end
  if widget.buf and vim.api.nvim_buf_is_valid(widget.buf) then
    pcall(vim.api.nvim_buf_delete, widget.buf, { force = true })
  end
end

local function with_widget(rows, opts, fn)
  local widget = open_widget(rows, opts)
  local ok, err = pcall(fn, widget)
  close_widget(widget)
  if not ok then error(err, 0) end
end

local function make_rows(state)
  state.flag = state.flag == nil and false or state.flag
  state.count = state.count or 1
  state.mode = state.mode or "off"
  state.ran = state.ran or 0
  return {
    {
      kind = "setting",
      label = "Flag",
      value_kind = "bool",
      get = function() return state.flag end,
      set = function(v) state.flag = v end,
    },
    {
      kind = "setting",
      label = "Count",
      value_kind = "int",
      min = 0,
      max = 5,
      get = function() return state.count end,
      set = function(v) state.count = v end,
    },
    {
      kind = "setting",
      label = "Mode",
      value_kind = "enum",
      values = { "off", "highlight", "above" },
      get = function() return state.mode end,
      set = function(v) state.mode = v end,
    },
    { kind = "separator" },
    {
      kind = "action",
      label = "Reset",
      run = function() state.ran = state.ran + 1 end,
    },
  }
end

test("settings_window: build_layout keeps separator spacing before actions", function()
  local layout = settings_window.build_layout({
    {
      kind = "setting",
      label = "display mode",
      value_kind = "enum",
      values = { "off", "above" },
      get = function() return "above" end,
      set = function() end,
    },
    { kind = "separator" },
    {
      kind = "action",
      label = "reset to default settings",
      run = function() end,
    },
  }, { row_prefix = "  ", value_gap = "   " })

  assert_eq(layout.lines[#layout.lines - 1], "")
  assert_eq(layout.lines[#layout.lines], "  reset to default settings")
end)

test("settings_window: keymaps adjust rows, prompt numeric input, and close", function()
  local state = { flag = false, count = 2, mode = "off", ran = 0, changes = 0 }
  local saved_guicursor = vim.o.guicursor

  with_widget(make_rows(state), {
    on_change = function() state.changes = state.changes + 1 end,
  }, function(widget)
    assert_match(vim.o.guicursor, "VimficiencySettingsCursorHidden")

    helpers.feed("<CR>")
    assert_eq(state.flag, true)

    helpers.feed("j")
    helpers.with_patch({
      { vim.ui, "input", function(_, cb) cb("4") end },
    }, function()
      helpers.feed("i")
    end)
    assert_eq(state.count, 4)

    helpers.feed("j")
    helpers.feed("<Tab>")
    assert_eq(state.mode, "highlight")
    helpers.feed("<S-Tab>")
    assert_eq(state.mode, "off")

    helpers.feed("j")
    helpers.feed("<CR>")
    assert_eq(state.ran, 1)
    assert_eq(state.changes, 5)

    helpers.feed("gs")
    assert_true(not vim.api.nvim_win_is_valid(widget.win))
  end)

  assert_eq(vim.o.guicursor, saved_guicursor)
end)

test("settings_window: buffer is locked and incidental motion is denied", function()
  local state = {}

  with_widget(make_rows(state), nil, function(widget)
    assert_eq(vim.bo[widget.buf].modifiable, false)
    local extmarks = vim.api.nvim_buf_get_extmarks(
      widget.buf, selection_ns, 0, -1, { details = true })
    assert_eq(#extmarks, 1)
    assert_eq(extmarks[1][4].hl_group, "CursorLine")

    helpers.feed("www")
    vim.wait(20)
    assert_eq(vim.api.nvim_win_get_cursor(widget.win), { 1, 0 })
    assert_eq(widget.handle.selection(), 1)
  end)
end)
