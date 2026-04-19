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

test("util.with_help_keymaps appends ? and g?", function()
  local maps = util.with_help_keymaps({
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close view" },
  }, "Example Keys", "vimficiency-inspecting-results-replay-buffer-keys")

  assert_eq(#maps, 3)
  assert_eq(maps[1].lhs, "q")
  assert_eq(maps[2].lhs, "?")
  assert_eq(maps[2].desc, "Show keymap summary")
  assert_eq(maps[3].lhs, "g?")
  assert_eq(maps[3].desc, "Open full help")
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
    help_tag = "vimficiency-commands-reference-scratch-output-buffer-keys",
    help_title = "Scratch Output Keys",
  })

  local maps = vim.api.nvim_buf_get_keymap(buf, "n")
  assert_true(find_map(maps, "q") ~= nil, "missing q map")
  assert_true(find_map(maps, "?") ~= nil, "missing ? map")
  assert_true(find_map(maps, "g?") ~= nil, "missing g? map")

  if vim.api.nvim_win_is_valid(win) then
    vim.api.nvim_win_close(win, true)
  end
end)
