local helpers = require("_helpers")
local util = require("vimficiency.util")

---@param maps table[]
---@param lhs string
---@return table?
local function find_map(maps, lhs)
  for _, m in ipairs(maps) do
    if m.lhs == lhs then return m end
  end
  return nil
end

test("util.with_standard_ui_keymaps appends ? and g?", function()
  local maps = util.with_standard_ui_keymaps({
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close view" },
  }, {
    title = "Example Keys",
    docs = true,
  })

  assert_eq(#maps, 3)
  assert_eq(maps[1].lhs, "q")
  assert_eq(maps[2].lhs, "?")
  assert_eq(maps[2].desc, "Show keymap summary")
  assert_eq(maps[3].lhs, "g?")
  assert_eq(maps[3].desc, "Full docs")
end)

test("util.show_keymap_help pairs adjacent summary groups", function()
  util.show_keymap_help("Example Keys", {
    { lhs = "<Left>",  handler = function() end, desc = "Step backward",
      summary_group = "step", summary_desc = "Step backward / forward" },
    { lhs = "<Right>", handler = function() end, desc = "Step forward",
      summary_group = "step", summary_desc = "Step backward / forward" },
    { lhs = "q",       handler = function() end, desc = "Close view" },
  })

  local buf = vim.api.nvim_get_current_buf()
  local lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
  assert_eq(lines[1], "  <Left>/<Right>  Step backward / forward")
  assert_eq(lines[2], "  q               Close view")

  local win = vim.api.nvim_get_current_win()
  if vim.api.nvim_win_is_valid(win) then
    vim.api.nvim_win_close(win, true)
  end
end)

test("util.set_buffer_keymaps requires desc", function()
  local buf = helpers.new_buf({ "x" })
  assert_error(function()
    util.set_buffer_keymaps(buf, {
      { lhs = "x", handler = function() end },
    })
  end, "require a desc")
end)

test("util.show_output installs help keymaps", function()
  local buf, win = util.show_output("Demo", "body", {
    ui_keys = {
      title = "Scratch Output Keys",
      docs = true,
    },
  })

  local maps = vim.api.nvim_buf_get_keymap(buf, "n")
  assert_true(find_map(maps, "q") ~= nil, "missing q map")
  assert_true(find_map(maps, "?") ~= nil, "missing ? map")
  assert_true(find_map(maps, "g?") ~= nil, "missing g? map")

  if vim.api.nvim_win_is_valid(win) then
    vim.api.nvim_win_close(win, true)
  end
end)

test("util.open_help falls back to top-level vimficiency help", function()
  local ok, err = pcall(function()
    util.open_help("vimficiency-definitely-missing-tag")
  end)
  assert_true(ok, err)

  local buf = vim.api.nvim_get_current_buf()
  local name = vim.api.nvim_buf_get_name(buf)
  assert_match(name, "doc/vimficiency.txt", "should land in top-level vimficiency help")

  local win = vim.api.nvim_get_current_win()
  if vim.api.nvim_win_is_valid(win) then
    vim.api.nvim_win_close(win, true)
  end
end)
