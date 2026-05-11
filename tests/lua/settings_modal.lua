local modal = require("vimficiency.settings_modal")

local function close_current_win()
  local win = vim.api.nvim_get_current_win()
  if vim.api.nvim_win_is_valid(win) then
    vim.api.nvim_win_close(win, true)
  end
end

test("settings_modal: opens the shared settings widget in a float", function()
  local handle = assert(modal.open({
    {
      kind = "setting",
      label = "count",
      value_kind = "int",
      min = 0,
      max = 5,
      get = function() return 1 end,
      set = function() end,
    },
  }, nil, { title = "Example Settings" }))

  assert_true(handle.is_open())

  local win = vim.api.nvim_get_current_win()
  local buf = vim.api.nvim_get_current_buf()
  assert_eq(vim.api.nvim_win_get_config(win).relative, "editor")

  local lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
  assert_match(lines[1], "count")
  assert_match(lines[1], "[1]")

  close_current_win()
end)
