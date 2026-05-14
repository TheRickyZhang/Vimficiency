local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore = require("vimficiency.explore")

local feed = helpers.feed
local first_recommendation_in_phase = explore_helpers.first_recommendation_in_phase

test("explore flow: natural motion updates cursor and completes motion-only goal", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-motion", "motion_word", function(scratch_buf)
      local motion = first_recommendation_in_phase("Navigate")
      feed(motion.text)
      explore_helpers.trigger_cursor_moved(scratch_buf)

      explore_helpers.wait_for("motion-only goal should complete", function()
        local st = explore.status()
        return st
          and st.is_completed
          and st.cursor.row == motion.landing.row
          and st.cursor.col == motion.landing.col
          and st.seq ~= ""
      end)
      assert_eq(explore.status().scratch_lines, { "foo bar" })
    end)
  end)
end)

test("explore flow: f space follows live cursor state", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-f-space", "find_space", function(scratch_buf)
      feed("f<Space>")
      explore_helpers.trigger_cursor_moved(scratch_buf)

      explore_helpers.wait_for("f<Space> should update the explored cursor", function()
        local st = explore.status()
        return st
          and st.is_completed
          and st.cursor.col == 3
          and st.scratch_cursor.col == 3
          and st.seq == "f<Space>"
      end)
    end)
  end)
end)

test("explore flow: live cursor move leaves transform range despite buffered keys", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-ciw-precursor", "replace_char", function()
      local view = explore_helpers.current_view()
      local handlers = require("vimficiency.explore.handlers")
      view.on_key_buffer = "ci"
      explore_helpers.set_scratch({ "abc" }, { 1, 0 })

      handlers.on_cursor_moved()

      local st = explore.status()
      assert_eq(st.phase.kind, "Navigate")
      assert_eq(view.on_key_buffer, "")
      assert_eq(st.scratch_cursor.col, 0)
    end)
  end)
end)
