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

local function find_windows_by_prefix(wins, prefix)
  local found = {}
  for _, win in ipairs(wins) do
    local buf = vim.api.nvim_win_get_buf(win)
    local name = vim.api.nvim_buf_get_name(buf)
    if name:sub(1, #prefix) == prefix and name ~= prefix .. "summary" then
      found[#found + 1] = { win = win, buf = buf, name = name }
    end
  end
  return found
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
      assert_eq(#wins, 6, "explore should open list, scratch, one shared header, and one header pane per displayed sequence")

      local header_prefix = "vimficiency://explore/demo/header/"
      local summary_name = "vimficiency://explore/demo/header/summary"
      local list_name = "vimficiency://explore/demo/recommendations"
      local scratch_name = "vimficiency://explore/demo"

      local header_wins = find_windows_by_prefix(wins, header_prefix)
      local summary_win, summary_buf = find_window_by_name(wins, summary_name)
      local list_win = find_window_by_name(wins, list_name)
      local scratch_win = find_window_by_name(wins, scratch_name)

      assert_eq(#header_wins, 3, "expected Explored, Optimal 1, and User typed header panes")
      assert_true(summary_win ~= nil, "missing shared summary header")
      assert_true(list_win ~= nil, "missing recommendations window")
      assert_true(scratch_win ~= nil, "missing scratch window")
      assert_true(vim.api.nvim_get_option_value("winfixheight", { win = summary_win }),
        "summary header should keep a fixed height")
      assert_true(not vim.bo[summary_buf].modifiable,
        "summary header buffer should stay read-only")
      local summary_lines = vim.api.nvim_buf_get_lines(summary_buf, 0, -1, false)
      assert_eq(summary_lines[2], "Explore demo")
      for _, header in ipairs(header_wins) do
        assert_true(vim.api.nvim_get_option_value("winfixheight", { win = header.win }),
          "header pane should keep a fixed height")
        assert_true(not vim.bo[header.buf].modifiable,
          "header buffer should stay read-only")
        local header_lines = vim.api.nvim_buf_get_lines(header.buf, 0, -1, false)
        assert_true(header_lines[1] == "Explored"
          or header_lines[1] == "Optimal 1"
          or header_lines[1] == "User typed",
          "per-sequence header should start with a sequence title")
      end

      local summary_pos = vim.api.nvim_win_get_position(summary_win)
      local header_pos = vim.api.nvim_win_get_position(header_wins[1].win)
      local scratch_pos = vim.api.nvim_win_get_position(scratch_win)
      assert_true(summary_pos[1] < header_pos[1],
        "shared summary should sit above the per-sequence header panes")
      assert_true(header_pos[1] < scratch_pos[1],
        "header pane should sit above the scratch pane")
    end)

    pcall(explore.cancel)
    if not ok then
      error(err, 0)
    end
  end)
end)
