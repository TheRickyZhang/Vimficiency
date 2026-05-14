local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore = require("vimficiency.explore")

local feed = helpers.feed
local list_has_warning = explore_helpers.list_has_warning
local with_notify_capture = explore_helpers.with_notify_capture
local has_plan_deviation_warning = explore_helpers.has_plan_deviation_warning
local complete_delete_mid_word = explore_helpers.complete_delete_mid_word

test("explore flow: bad insert entry snaps back with plan warning", function()
  with_notify_capture(function(notices)
    explore_helpers.open_scenario_flow("flow-bad-insert-entry", "delete_mid_word", function(scratch_buf)
      local handlers, view = complete_delete_mid_word(scratch_buf)

      explore_helpers.set_scratch({ "ab de" }, { 1, 4 })
      view.on_key_buffer = "a"
      handlers.on_insert_enter()

      explore_helpers.wait_for("bad insert should finish scheduled restore", function()
        local st = explore.status()
        return view.restoring == nil
          and st
          and st.scratch_cursor.col == 3
      end)

      local st = explore.status()
      assert_true(st.warning == nil, "ordinary rejected insert should not set invariant warning")
      assert_true(st.is_completed, "bad insert should leave backend completed")
      assert_eq(st.scratch_lines, { "ab de" })
      assert_eq(st.scratch_cursor.col, 3)
      assert_eq(view.on_key_buffer, "")
      assert_true(has_plan_deviation_warning(notices, "outside planned edit range"),
        "ordinary rejected insert should emit a plan-deviation warning")
      assert_true(not list_has_warning(view),
        "ordinary rejected insert should not render the invariant warning")
    end)
  end)
end)

test("explore flow: fed append key restores completed cursor state", function()
  with_notify_capture(function(notices)
    explore_helpers.open_scenario_flow("flow-fed-append-restore", "delete_mid_word", function(scratch_buf)
      local _, view = complete_delete_mid_word(scratch_buf)

      feed("a")

      explore_helpers.wait_for("fed append should restore completed cursor", function()
        local st = explore.status()
        local mode = vim.api.nvim_get_mode().mode
        return view.restoring == nil
          and st
          and st.is_completed
          and st.warning == nil
          and st.scratch_cursor.col == 3
          and st.scratch_cursor.row == 0
          and st.scratch_lines[1] == "ab de"
          and mode:sub(1, 1) ~= "i"
          and mode:sub(1, 1) ~= "R"
      end)

      assert_true(has_plan_deviation_warning(notices, "outside planned edit range"),
        "fed append should emit a plan-deviation warning")
      assert_true(not list_has_warning(view),
        "fed append should not render the invariant warning")
    end)
  end)
end)

test("explore flow: fed motion delete then append restores delete cursor state", function()
  with_notify_capture(function(notices)
    explore_helpers.open_scenario_flow("flow-fed-wx-a-restore", "delete_mid_word", function(scratch_buf)
      local view = explore_helpers.current_view()

      feed("w")
      explore_helpers.trigger_cursor_moved(scratch_buf)
      feed("x")
      vim.api.nvim_exec_autocmds("TextChanged", { buffer = scratch_buf, modeline = false })
      explore_helpers.wait_for("fed wx should complete at delete cursor", function()
        local st = explore.status()
        return st
          and st.is_completed
          and st.cursor.col == 3
          and st.scratch_cursor.col == 3
          and st.scratch_lines[1] == "ab de"
      end)

      feed("a")

      explore_helpers.wait_for("fed append should restore the completed delete cursor", function()
        local st = explore.status()
        local mode = vim.api.nvim_get_mode().mode
        return view.restoring == nil
          and st
          and st.is_completed
          and st.warning == nil
          and st.scratch_cursor.row == 0
          and st.scratch_cursor.col == 3
          and st.cursor.col == 3
          and st.scratch_lines[1] == "ab de"
          and mode:sub(1, 1) ~= "i"
          and mode:sub(1, 1) ~= "R"
      end)

      assert_true(has_plan_deviation_warning(notices, "outside planned edit range"),
        "fed append should emit a plan-deviation warning")
      assert_true(not list_has_warning(view),
        "fed append should not render the invariant warning")
    end)
  end)
end)

test("explore flow: bad insert entry exits insert before cursor restore", function()
  with_notify_capture(function()
    explore_helpers.open_scenario_flow("flow-bad-insert-mode-restore", "delete_mid_word", function(scratch_buf)
      local state_mod = require("vimficiency.explore.state")
      local handlers, view = complete_delete_mid_word(scratch_buf)

      local real_cmd = vim.cmd
      local real_reload = state_mod.reload_buffer
      local real_refresh = state_mod.refresh_ui
      local mode = "i"
      local calls = {}

      helpers.with_patch({
        { vim.api, "nvim_get_mode", function()
          return { mode = mode, blocking = false }
        end },
        { vim, "cmd", function(cmd, ...)
          if cmd == "stopinsert" then
            calls[#calls + 1] = "stopinsert"
            mode = "n"
            return
          end
          return real_cmd(cmd, ...)
        end },
        { state_mod, "reload_buffer", function(active)
          calls[#calls + 1] = "reload:" .. mode
          return real_reload(active)
        end },
        { state_mod, "refresh_ui", function(active)
          calls[#calls + 1] = "refresh:" .. mode
          return real_refresh(active)
        end },
      }, function()
        explore_helpers.set_scratch({ "ab de" }, { 1, 4 })
        view.on_key_buffer = "a"
        handlers.on_insert_enter()

        explore_helpers.wait_for("rejected insert should finish scheduled restore", function()
          return view.restoring == nil
            and calls[#calls] == "refresh:n"
        end)
      end)

      assert_eq(calls[1], "stopinsert")
      assert_eq(calls[2], "reload:n")
      assert_eq(calls[3], "refresh:n")

      local st = explore.status()
      assert_eq(st.scratch_lines, { "ab de" })
      assert_eq(st.scratch_cursor.col, 3)
      assert_true(st.warning == nil, "scheduled restore should not trip invariant warning")
    end)
  end)
end)

test("explore flow: overdelete snaps back with plan warning", function()
  with_notify_capture(function(notices)
    explore_helpers.open_scenario_flow("flow-overdelete", "overdelete_multiline", function()
      local handlers = require("vimficiency.explore.handlers")
      local view = explore_helpers.current_view()
      assert_eq(explore.status().phase.kind, "Transform")

      explore_helpers.set_scratch({ "def" }, { 1, 0 })
      view.on_key_buffer = "dd"
      handlers.on_buffer_changed()

      local st = explore.status()
      assert_true(st.warning == nil, "ordinary rejected overdelete should not set invariant warning")
      assert_eq(st.phase.kind, "Transform")
      assert_eq(st.seq, "")
      assert_eq(st.session_lines, { "abc", "def" })
      assert_eq(st.scratch_lines, { "abc", "def" })
      assert_eq(st.scratch_cursor.row, 0)
      assert_eq(st.scratch_cursor.col, 2)
      assert_eq(view.on_key_buffer, "")
      assert_true(has_plan_deviation_warning(notices, "planned edit scope"),
        "ordinary rejected overdelete should emit a plan-deviation warning")
      assert_true(not list_has_warning(view),
        "ordinary rejected overdelete should not render the invariant warning")
    end)
  end)
end)
