-- Full-screen result viewer: layout, content, and diff/cursor extmarks.
-- Opens a real tab + panes, asserts structure, then tears the tab down so the
-- shared test Neovim returns to baseline.

local v = vim.api
local h = require("_helpers")
local result_window = require("vimficiency.session.result_window")

local ns = v.nvim_create_namespace("vimfy_result_view")

local function open_fixture(overrides)
  local result = h.fake_result(vim.tbl_extend("force", {
    lines = { "aaa bbb", "ccc" },
    goal_lines = { "aaa zzz", "ccc" },
    start_row = 0, start_col = 0,
    end_row = 0, end_col = 6,
    has_lines_above = true,
    has_lines_below = true,
    prefix = "",
    suffix = "",
    user_seq = "x",
    user_cost = 12,
    optimal_results = { { seq = "y", cost = 4 } },
    diffs = {
      { init = { begin_row = 0, begin_col = 4, end_row = 0, end_col = 7 },
        goal = { begin_row = 0, begin_col = 4, end_row = 0, end_col = 7 } },
    },
  }, overrides or {}))

  local before = #v.nvim_list_tabpages()
  result_window.open("test", result)
  return before
end

--- Close the current (view) tab, restoring the prior one.
local function close_view_tab()
  if #v.nvim_list_tabpages() > 1 then
    vim.cmd("tabclose")
  end
end

test("result_window: opens a 3-pane tab with initial/final content", function()
  local before = open_fixture()
  local wins = v.nvim_tabpage_list_wins(0)
  assert_eq(#v.nvim_list_tabpages(), before + 1, "a new tab is created")
  assert_eq(#wins, 3, "header + left + right")

  -- Find the panes by buffer name suffix.
  local found = {}
  for _, w in ipairs(wins) do
    local name = v.nvim_buf_get_name(v.nvim_win_get_buf(w))
    if name:find("/initial$") then found.left = v.nvim_win_get_buf(w) end
    if name:find("/final$") then found.right = v.nvim_win_get_buf(w) end
    if name:find("/header$") then found.header = v.nvim_win_get_buf(w) end
  end
  assert_true(found.left and found.right and found.header, "all three panes present")

  assert_eq(v.nvim_buf_get_lines(found.left, 0, -1, false)[1], "aaa bbb")
  assert_eq(v.nvim_buf_get_lines(found.right, 0, -1, false)[1], "aaa zzz")

  close_view_tab()
end)

test("result_window: highlights the diff columns on both panes", function()
  open_fixture()
  local wins = v.nvim_tabpage_list_wins(0)
  local left, right
  for _, w in ipairs(wins) do
    local b = v.nvim_win_get_buf(w)
    local name = v.nvim_buf_get_name(b)
    if name:find("/initial$") then left = b end
    if name:find("/final$") then right = b end
  end

  local left_marks = v.nvim_buf_get_extmarks(left, ns, 0, -1, {})
  local right_marks = v.nvim_buf_get_extmarks(right, ns, 0, -1, {})
  -- Each pane carries at least a diff span + a cursor cell + context markers.
  assert_true(#left_marks >= 2, "initial pane has diff + cursor extmarks")
  assert_true(#right_marks >= 2, "final pane has diff + cursor extmarks")

  close_view_tab()
end)

test("result_window: recomputes diffs when the result lacks them", function()
  -- nil diffs (e.g. a pre-diff saved file) must not error; the view recomputes.
  open_fixture({ diffs = nil })
  assert_true(#v.nvim_tabpage_list_wins(0) == 3, "view still opens with recomputed diffs")
  close_view_tab()
end)

test("result_window: header carries the mouse warning when had_mouse", function()
  open_fixture({ had_mouse = true })
  local header
  for _, w in ipairs(v.nvim_tabpage_list_wins(0)) do
    local b = v.nvim_win_get_buf(w)
    if v.nvim_buf_get_name(b):find("/header$") then header = b end
  end
  local text = table.concat(v.nvim_buf_get_lines(header, 0, -1, false), "\n")
  assert_true(text:find("mouse/scroll") ~= nil, "header warns about mouse input")
  close_view_tab()
end)
