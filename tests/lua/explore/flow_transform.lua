local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore = require("vimficiency.explore")

local first_recommendation_in_phase = explore_helpers.first_recommendation_in_phase
local widen_recommendations = explore_helpers.widen_recommendations
local key_event = explore_helpers.key_event

test("explore flow: normal deletion snapshot stays transform and suggests insertion", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-x-then-insert", "single_char_change", function()
      local view = widen_recommendations()
      first_recommendation_in_phase("Transform", function(rec)
        return rec.text == "x"
      end)
      local handlers = require("vimficiency.explore.handlers")
      view.on_key_buffer = "x"
      explore_helpers.set_scratch({ "" })

      handlers.on_buffer_changed()

      local st = explore.status()
      assert_eq(st.phase.kind, "Transform")
      assert_eq(view.on_key_buffer, "")
      assert_eq(st.scratch_lines[1], "")
      first_recommendation_in_phase("Transform", function(rec)
        return rec.text == "i" or rec.text == "I"
      end)
    end)
  end)
end)

test("explore flow: operator-pending duplicate keys are reduced before snapshot", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-diw-dedup", "delete_text_object", function()
      local handlers = require("vimficiency.explore.handlers")
      local view = explore_helpers.current_view()
      view.on_key_events = {
        key_event("n", "d"),
        key_event("no", "i"),
        key_event("n", "i"),
        key_event("n", "w"),
      }
      explore_helpers.set_scratch({ "" })

      handlers.on_buffer_changed()

      local st = explore.status()
      assert_eq(st.seq, "diw")
      assert_eq(view.on_key_buffer, "")
      assert_eq(#view.on_key_events, 0)
    end)
  end)
end)
