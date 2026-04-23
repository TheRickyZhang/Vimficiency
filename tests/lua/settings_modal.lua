local helpers = require("_helpers")
local modal = require("vimficiency.settings_modal")

local function close_current_win()
  local win = vim.api.nvim_get_current_win()
  if vim.api.nvim_win_is_valid(win) then
    vim.api.nvim_win_close(win, true)
  end
end

test("settings_modal: reset action is separated by a blank line", function()
  modal.open({
    {
      kind = "setting",
      label = "display mode",
      value_kind = "enum",
      values = { "off", "above" },
      get = function() return "above" end,
      set = function() end,
    },
    {
      kind = "separator",
    },
    {
      kind = "action",
      label = "reset to default settings",
      run = function() end,
    },
  }, nil, { title = "Example Settings" })

  local buf = vim.api.nvim_get_current_buf()
  local lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
  assert_eq(lines[#lines - 1], "")
  assert_eq(lines[#lines], "  reset to default settings")

  close_current_win()
end)

test("settings_modal: enum expansion wraps within the value column", function()
  local util = require("vimficiency.util")

  helpers.with_patch({
    { util, "centered_popup_geometry", function() return 34, 12, 0, 0 end },
  }, function()
    modal.open({
      {
        kind = "setting",
        label = "display mode",
        value_kind = "enum",
        values = { "off", "highlight", "inplace", "above", "below" },
        get = function() return "above" end,
        set = function() end,
      },
    }, nil, { title = "Example Settings" })

    local buf = vim.api.nvim_get_current_buf()
    local extmarks = vim.api.nvim_buf_get_extmarks(buf,
      vim.api.nvim_create_namespace("vimficiency_settings_expansion"),
      0, -1, { details = true })
    local virt_lines = extmarks[1][4].virt_lines
    local indent = string.rep(" ", #"display mode" + 5)

    assert_true(#virt_lines > 1, "enum choices should wrap when width is constrained")
    assert_eq(virt_lines[1][1][1], indent)
    assert_eq(virt_lines[2][1][1], indent)

    close_current_win()
  end)
end)
