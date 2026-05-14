local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore = require("vimficiency.explore")

local feed = helpers.feed
local first_recommendation_in_phase = explore_helpers.first_recommendation_in_phase
local enters_insert_mode = explore_helpers.enters_insert_mode
local header_lines_for_title = explore_helpers.header_lines_for_title
local move_to_first_edit_target = explore_helpers.move_to_first_edit_target

test("explore flow: insert recommendation accepts matching scratch edit", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-insert-ok", "basic_insert", function(scratch_buf)
      move_to_first_edit_target(scratch_buf)

      -- Feed structural + typed + <Esc> as one unit. The fake_result's
      -- diff is "abc" → "aBc", so the picked structural's deletion range
      -- determines the required typed continuation. For `ce`/`C`-style
      -- structurals (which delete "bc"), the typed continuation is "Bc".
      -- We can't observe intermediate Insert state in headless tests
      -- because feedkeys "x" mode triggers an implicit InsertLeave when
      -- it returns mid-insert, so we feed everything at once.
      local edit = first_recommendation_in_phase("Transform", enters_insert_mode)
      feed(edit.text .. "Bc<Esc>")

      -- After Stage 2: completion requires the cursor to actually reach
      -- goalPos, not merely "all edits applied". Insert exits at the
      -- post-typed-text cursor (here col 2), so the session lands in
      -- Navigate(totalEdits) — buffer matches goal, but cursor still has
      -- to travel. Assert the buffer-shape advance, not is_completed.
      explore_helpers.wait_for("matching insert should reach the goal buffer", function()
        local st = explore.status()
        return st
          and st.phase.kind == "Navigate"
          and st.phase.edit_index == st.total_edits
          and vim.deep_equal(st.session_lines, { "aBc" })
          and vim.deep_equal(st.scratch_lines, { "aBc" })
          and st.pending == nil
      end)
    end)
  end)
end)

test("explore flow: ciw advances through swallowed text-object key", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-ciw-insert-ok", "single_char_change", function()
      feed("ciwm<Esc>")

      explore_helpers.wait_for("ciw insert should reach the goal buffer", function()
        local st = explore.status()
        return st
          and st.is_completed
          and vim.deep_equal(st.session_lines, { "m" })
          and vim.deep_equal(st.scratch_lines, { "m" })
          and st.pending == nil
      end)
    end)
  end)
end)

test("explore flow: insert-enter advances from expected buffer, not raw keys", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-insert-buffer-gate", "single_char_change", function()
      local handlers = require("vimficiency.explore.handlers")
      local view = explore_helpers.current_view()
      view.on_key_buffer = "ci"
      explore_helpers.set_scratch({ "" })

      handlers.on_insert_enter()

      local st = explore.status()
      assert_eq(st.phase.kind, "Insert")
      assert_eq(st.pending.target, "m")
      assert_eq(st.scratch_lines[1], "")
    end)
  end)
end)

test("explore flow: structural TextChanged during insert does not cancel typing phase", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-insert-textchanged", "single_char_change", function()
      local handlers = require("vimficiency.explore.handlers")
      local view = explore_helpers.current_view()
      view.on_key_buffer = "ci"
      explore_helpers.set_scratch({ "" })

      handlers.on_insert_enter()
      handlers.on_buffer_changed({ event = "TextChanged" })

      local st = explore.status()
      assert_eq(st.phase.kind, "Insert")
      assert_eq(st.pending.target, "m")
      assert_eq(st.scratch_lines[1], "")
    end)
  end)
end)

test("explore flow: live insert prefix appears in explored header", function()
  helpers.silence_notify(function()
    local label = "flow-insert-live-header"
    explore_helpers.open_scenario_flow(label, "insert_expression", function(scratch_buf)
      local header_render = require("vimficiency.explore.render.header")
      local insert_helpers = require("vimficiency.explore.insert_helpers")
      local view = explore_helpers.current_view()

      view.state.phase = { kind = "Insert", edit_index = 0 }
      view.header_rows.explored = { "ciw" }
      view.header_rows.optimal = {}
      view.pending = {
        target = "2<Space>*<Space>i",
        literal_target = "2 * i",
        row = 0,
        col_start = 0,
      }
      view.on_key_buffer = "ciw2<Space>"
      explore_helpers.set_scratch({ "2 * i" }, { 1, 2 })

      header_render.render(view, insert_helpers.current_continuation(view))

      local lines = header_lines_for_title(scratch_buf, "Explored")
      assert_eq(lines[3], "ciw")
      assert_eq(lines[4], "ciw 2␣")
    end)
  end)
end)

test("explore flow: insert mismatch suggests backspace repair", function()
  helpers.silence_notify(function()
    local label = "flow-insert-backspace-repair"
    explore_helpers.open_scenario_flow(label, "insert_expression", function()
      local header_render = require("vimficiency.explore.render.header")
      local insert_helpers = require("vimficiency.explore.insert_helpers")
      local list_render = require("vimficiency.explore.render.list")
      local view = explore_helpers.current_view()

      view.state.phase = { kind = "Insert", edit_index = 0 }
      view.pending = {
        target = "2<Space>*<Space>i",
        literal_target = "2 * i",
        row = 0,
        col_start = 0,
      }
      view.on_key_buffer = "ciw2x"
      explore_helpers.set_scratch({ "2x * i" }, { 1, 2 })

      local continuation = insert_helpers.current_continuation(view)
      header_render.render(view, continuation)
      list_render.render(view, continuation)

      local list_lines = vim.api.nvim_buf_get_lines(view.list_buf, 0, -1, false)
      assert_true(
        table.concat(list_lines, "\n"):find("<BS> ␣%*␣i", 1) ~= nil,
        "insert suggestion should repair the astray suffix with backspace")
    end)
  end)
end)

test("explore flow: insert mismatch reverts scratch buffer", function()
  helpers.silence_notify(function()
    explore_helpers.open_scenario_flow("flow-insert-bad", "basic_insert", function(scratch_buf)
      move_to_first_edit_target(scratch_buf)

      -- Feed structural + WRONG typed + <Esc>. Same shape as the matching
      -- variant, but with a typed continuation that doesn't reach goal.
      local edit = first_recommendation_in_phase("Transform", enters_insert_mode)
      feed(edit.text .. "X<Esc>")

      explore_helpers.wait_for("mismatched insert should revert to the current fencepost", function()
        local st = explore.status()
        return st
          and (st.phase.kind == "Navigate" or st.phase.kind == "Transform")
          and vim.deep_equal(st.session_lines, { "abc" })
          and vim.deep_equal(st.scratch_lines, { "abc" })
          and st.pending == nil
      end)
    end)
  end)
end)
