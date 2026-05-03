-- Tests for the explore settings side panel. The panel module is
-- self-contained: it takes a fake "view" with just a `scratch.win`
-- field, plus a schema and on_change callback, and manages its own
-- buffer + keymaps.
--
-- We exercise it directly (rather than driving through `gs` on a real
-- explore session) because:
--   1. The panel module is the one with novel behavior; the gs wiring
--      is a one-line keymap.
--   2. A real explore session needs FFI + a captured analyze result;
--      that infrastructure is exercised by explore_flow.lua.
local helpers = require("_helpers")
local panel = require("vimficiency.explore.render.panel")

local function make_view()
  -- Anchor window for the panel's split. Use a regular scratch buffer
  -- so the vsplit lands in a predictable layout.
  local buf = helpers.new_buf({ "scratch" })
  return {
    scratch = { win = vim.api.nvim_get_current_win(), buf = buf },
    panel = nil,
  }
end

local function teardown(view)
  pcall(panel.close, view)
  pcall(vim.cmd, "silent! tabonly")
  pcall(vim.cmd, "silent! %bwipeout!")
end

local function make_schema(state)
  -- Minimal schema covering the three value kinds + an action row. The
  -- `state` table is the test's view of what the user sees / changes.
  state = state or {}
  state.flag = state.flag == nil and false or state.flag
  state.count = state.count or 1
  state.mode = state.mode or "off"
  state.reset_called = state.reset_called or 0
  return {
    { kind = "setting",
      label = "Flag",
      value_kind = "bool",
      get = function() return state.flag end,
      set = function(v) state.flag = v end },
    { kind = "setting",
      label = "Count",
      value_kind = "int", min = 0, max = 5,
      get = function() return state.count end,
      set = function(v) state.count = v end },
    { kind = "setting",
      label = "Mode",
      value_kind = "enum", values = { "off", "highlight", "above" },
      get = function() return state.mode end,
      set = function(v) state.mode = v end },
    { kind = "separator" },
    { kind = "action",
      label = "Reset",
      run = function() state.reset_called = state.reset_called + 1 end },
  }, state
end

test("explore panel: open creates buffer and renders all setting rows", function()
  local view = make_view()
  local schema, state = make_schema()
  panel.open(view, schema, function() end)

  assert_true(panel.is_open(view))
  local lines = vim.api.nvim_buf_get_lines(view.panel.buf, 0, -1, false)
  -- 3 setting rows + 1 separator (blank) + 1 action = 5 lines.
  assert_eq(#lines, 5)
  assert_match(lines[1], "Flag")
  assert_match(lines[1], "[ ]")  -- bool=false renders as `[ ]`
  assert_match(lines[2], "Count")
  assert_match(lines[2], "[1]")
  assert_match(lines[3], "Mode")
  assert_match(lines[3], "[off]")
  assert_eq(lines[4], "")        -- separator
  assert_match(lines[5], "Reset")

  -- Initial selection lands on the first selectable row.
  assert_eq(view.panel.selection, 1)
  _ = state

  teardown(view)
end)

test("explore panel: close tears down window and clears panel state", function()
  local view = make_view()
  local schema = make_schema()
  panel.open(view, schema, function() end)
  local panel_win = view.panel.win
  panel.close(view)

  assert_true(view.panel == nil)
  assert_true(not vim.api.nvim_win_is_valid(panel_win))
  assert_true(not panel.is_open(view))

  teardown(view)
end)

test("explore panel: re-opening on already-open view re-renders in place", function()
  local view = make_view()
  local schema_a = make_schema({ flag = false })
  panel.open(view, schema_a, function() end)
  local first_buf = view.panel.buf

  -- Second open with a different schema. The panel should reuse the
  -- existing window (not stack two splits) and update its rows.
  local schema_b, state_b = make_schema({ flag = true })
  panel.open(view, schema_b, function() end)

  assert_eq(view.panel.buf, first_buf)
  local lines = vim.api.nvim_buf_get_lines(view.panel.buf, 0, -1, false)
  assert_match(lines[1], "[✓]")  -- new schema's flag = true
  _ = state_b

  teardown(view)
end)

test("explore panel: refresh re-reads getters after external state change", function()
  local view = make_view()
  local schema, state = make_schema({ count = 2 })
  panel.open(view, schema, function() end)

  -- External mutation — the closure-captured state changed without
  -- going through a panel keymap. `refresh` must pull fresh values.
  state.count = 4
  panel.refresh(view)

  local lines = vim.api.nvim_buf_get_lines(view.panel.buf, 0, -1, false)
  assert_match(lines[2], "[4]")

  teardown(view)
end)

test("explore panel: refresh on closed panel is a no-op", function()
  local view = make_view()
  -- panel.refresh must not error when the panel was never opened or
  -- has been closed — callers (e.g. settings reset) call it
  -- unconditionally.
  panel.refresh(view)
  assert_true(view.panel == nil)

  teardown(view)
end)
