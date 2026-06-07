local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")

test("explore.open creates a 2-pane tab with the header rendered as virt_lines on scratch", function()
  helpers.silence_notify(function()
    local explore = require("vimficiency.explore")
    local result = explore_helpers.result("layout_multiline")

    local ok, err = pcall(function()
      helpers.new_buf(result.lines)
      assert_true(explore.open("demo", result), "explore.open should succeed")

      local wins = vim.api.nvim_tabpage_list_wins(vim.api.nvim_get_current_tabpage())
      assert_eq(#wins, 2,
        "explore should open exactly the list and scratch panes; header is virt_lines")

      local list_name = "vimficiency://explore/demo/recommendations"
      local scratch_name = "vimficiency://explore/demo"
      local list_win = explore_helpers.find_window_by_name(list_name, wins)
      local scratch_win, scratch_buf = explore_helpers.find_window_by_name(scratch_name, wins)
      assert_true(list_win ~= nil, "missing recommendations window")
      assert_true(scratch_win ~= nil, "missing scratch window")
      scratch_buf = assert(scratch_buf, "missing scratch buffer")

      -- The plan computes on a worker thread; the header virt_lines are only
      -- rendered once the poller installs the view. Pump the loop until ready.
      local view = require("vimficiency.explore.registry").current()
      assert_true(vim.wait(5000, function() return view.view_id ~= nil end, 5),
        "explore plan did not finish computing")

      local strings = assert(explore_helpers.header_virt_strings(scratch_buf),
        "scratch buffer should carry the header virt_lines extmark")

      local titles = {}
      for _, line in ipairs(strings) do
        for _, title in ipairs({ "Explored", "User typed", "Optimal 1" }) do
          if line:find(title, 1, true) then titles[title] = true end
        end
      end
      assert_true(titles["Explored"], "header should include the Explored column")
      assert_true(titles["User typed"], "header should include the User typed column")
      assert_true(titles["Optimal 1"], "header should include the Optimal 1 column")

      local has_summary = false
      for _, line in ipairs(strings) do
        if line:find("Explore demo", 1, true) then has_summary = true; break end
      end
      assert_true(has_summary, "header should include the 'Explore <label>' summary line")
    end)

    pcall(explore.cancel)
    if not ok then
      error(err, 0)
    end
  end)
end)
