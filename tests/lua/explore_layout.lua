local helpers = require("_helpers")

local function find_window_by_name(wins, expected_name)
  for _, win in ipairs(wins) do
    local buf = vim.api.nvim_win_get_buf(win)
    if vim.api.nvim_buf_get_name(buf) == expected_name then
      return win, buf
    end
  end
  return nil, nil
end

test("explore.open creates a fixed header pane above the scratch pane", function()
  helpers.silence_notify(function()
    local explore = require("vimficiency.explore")
    local result = helpers.fake_result({
      lines = { "alpha", "beta", "gamma", "delta" },
      goal_lines = { "alpha", "beta", "gamma", "delta" },
      user_seq = "j",
      optimal_results = {
        { seq = "k", cost = 1.0 },
        { seq = "gg", cost = 2.0 },
      },
    })

    local ok, err = pcall(function()
      helpers.new_buf(result.lines)
      assert_true(explore.open("demo", result), "explore.open should succeed")

      local wins = vim.api.nvim_tabpage_list_wins(vim.api.nvim_get_current_tabpage())
      assert_eq(#wins, 3, "explore should open list, header, and scratch panes")

      local header_name = "vimficiency://explore/demo/header"
      local list_name = "vimficiency://explore/demo/recommendations"
      local scratch_name = "vimficiency://explore/demo"

      local header_win, header_buf = find_window_by_name(wins, header_name)
      local list_win = find_window_by_name(wins, list_name)
      local scratch_win = find_window_by_name(wins, scratch_name)

      assert_true(header_win ~= nil, "missing header window")
      assert_true(list_win ~= nil, "missing recommendations window")
      assert_true(scratch_win ~= nil, "missing scratch window")
      assert_true(vim.api.nvim_get_option_value("winfixheight", { win = header_win }),
        "header pane should keep a fixed height")
      assert_true(not vim.bo[header_buf].modifiable,
        "header buffer should stay read-only")

      local header_lines = vim.api.nvim_buf_get_lines(header_buf, 0, -1, false)
      assert_eq(header_lines[2], "Explore demo")

      local header_pos = vim.api.nvim_win_get_position(header_win)
      local scratch_pos = vim.api.nvim_win_get_position(scratch_win)
      assert_true(header_pos[1] < scratch_pos[1],
        "header pane should sit above the scratch pane")
    end)

    pcall(explore.cancel)
    if not ok then
      error(err, 0)
    end
  end)
end)
