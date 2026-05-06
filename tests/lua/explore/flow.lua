local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore = require("vimficiency.explore")

local feed = helpers.feed

-- Recs are kind-pure per phase: Navigate emits motions, Transform emits edit
-- atoms, Insert emits the canonical typed text. Caller asserts the phase.
local function first_recommendation_in_phase(expected_phase, pred)
  local st = explore.status()
  assert_true(st ~= nil, "explore status should be available")
  assert_eq(st.phase.kind, expected_phase,
    "recommendations sourced from phase " .. expected_phase)
  for _, rec in ipairs(st.recommendations) do
    if not pred or pred(rec) then return rec end
  end
  error("no recommendation matched in phase " .. expected_phase ..
        "\nstatus=" .. explore_helpers.status_text(), 2)
end

local function enters_insert_mode(rec)
  -- The typed-text content moved into the Insert-phase rec; here we only
  -- check the structural prefix's first char.
  local first = rec.text:sub(1, 1)
  return first == "C" or first == "c" or first == "s"
    or first == "i" or first == "a" or first == "A"
    or first == "I" or first == "o" or first == "O"
end

local function widen_recommendations()
  local registry = require("vimficiency.explore.registry")
  local state = require("vimficiency.explore.state")
  local view = registry.current()
  view.recommendation_count = 10
  state.refresh_ui(view)
  return view
end

local function list_has_warning(view)
  local list_lines = vim.api.nvim_buf_get_lines(view.list_buf, 0, -1, false)
  return table.concat(list_lines, "\n"):find("EXPLORE STATE WARNING", 1, true) ~= nil
end

local function header_lines_for_title(scratch_buf, title)
  local lines = explore_helpers.header_column_lines(scratch_buf, title)
  if not lines then error("missing header column " .. title, 2) end
  return lines
end

local function with_notify_capture(fn)
  local util = require("vimficiency.util")
  local notices = {}
  helpers.with_patch({
    { vim, "notify", function(msg, level)
      notices[#notices + 1] = { msg = tostring(msg), level = level }
    end },
    { util, "show_output", function() end },
  }, function()
    fn(notices)
  end)
end

---@param mode string
---@param key_typed string
---@return VF.KeyEvent
local function key_event(mode, key_typed)
  return {
    t = 0,
    mode = mode,
    win = 0,
    buf = 0,
    key_sent_raw = key_typed,
    key_sent = key_typed,
    key_typed_raw = key_typed,
    key_typed = key_typed,
  }
end

local function has_plan_deviation_warning(notices, reason)
  for _, notice in ipairs(notices) do
    if notice.level == vim.log.levels.WARN
        and notice.msg:find("action deviated from the plan", 1, true)
        and notice.msg:find(reason, 1, true) then
      return true
    end
  end
  return false
end

local function move_to_first_edit_target(scratch_buf)
  local target = explore.status()
  local motion = first_recommendation_in_phase("Navigate", function(rec)
    return target.target_range
      and rec.landing.row == target.target_range.begin_pos.row
      and rec.landing.col == target.target_range.begin_pos.col
  end)
  feed(motion.text)
  explore_helpers.trigger_cursor_moved(scratch_buf)
  explore_helpers.wait_for("motion recommendation should update FFI cursor", function()
    local st = explore.status()
    return st
      and st.cursor.row == motion.landing.row
      and st.cursor.col == motion.landing.col
      and st.seq ~= ""
  end)
end

test("explore flow: natural motion updates cursor and completes motion-only goal", function()
  helpers.silence_notify(function()
    explore_helpers.open_flow("flow-motion", explore_helpers.fake_result({
      lines = { "foo bar" },
      goal_lines = { "foo bar" },
      end_col = 4,
      user_seq = "w",
      optimal_results = { { seq = "w", cost = 1.0 } },
    }), function(scratch_buf)
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
    explore_helpers.open_flow("flow-f-space", explore_helpers.fake_result({
      lines = { "abc def" },
      goal_lines = { "abc def" },
      end_col = 3,
      user_seq = "f ",
      optimal_results = { { seq = "f ", cost = 1.0 } },
    }), function(scratch_buf)
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

test("explore flow: insert recommendation accepts matching scratch edit", function()
  helpers.silence_notify(function()
    explore_helpers.open_flow("flow-insert-ok", explore_helpers.fake_result(), function(scratch_buf)
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
    explore_helpers.open_flow("flow-ciw-insert-ok", explore_helpers.fake_result({
      lines = { "n" },
      goal_lines = { "m" },
      end_col = 0,
      user_seq = "sm<Esc>",
      optimal_results = { { seq = "sm<Esc>", cost = 1.0 } },
    }), function()
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
    explore_helpers.open_flow("flow-insert-buffer-gate", explore_helpers.fake_result({
      lines = { "n" },
      goal_lines = { "m" },
      end_col = 0,
      user_seq = "sm<Esc>",
      optimal_results = { { seq = "sm<Esc>", cost = 1.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local registry = require("vimficiency.explore.registry")
      local view = registry.current()
      view.on_key_buffer = "ci"
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "" })

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
    explore_helpers.open_flow("flow-insert-textchanged", explore_helpers.fake_result({
      lines = { "n" },
      goal_lines = { "m" },
      end_col = 0,
      user_seq = "sm<Esc>",
      optimal_results = { { seq = "sm<Esc>", cost = 1.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local registry = require("vimficiency.explore.registry")
      local view = registry.current()
      view.on_key_buffer = "ci"
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "" })

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
    explore_helpers.open_flow(label, explore_helpers.fake_result({
      lines = { "cout << i << endl;" },
      goal_lines = { "cout << 2 * i << endl;" },
      end_col = 12,
      user_seq = "ciw2<Space>*<Space>i<Esc>",
      optimal_results = { { seq = "ciw2<Space>*<Space>i<Esc>", cost = 1.0 } },
    }), function(scratch_buf)
      local header_render = require("vimficiency.explore.render.header")
      local insert_helpers = require("vimficiency.explore.insert_helpers")
      local view = require("vimficiency.explore.registry").current()

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
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "2 * i" })
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 2 })

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
    explore_helpers.open_flow(label, explore_helpers.fake_result({
      lines = { "cout << i << endl;" },
      goal_lines = { "cout << 2 * i << endl;" },
      end_col = 12,
      user_seq = "ciw2<Space>*<Space>i<Esc>",
      optimal_results = { { seq = "ciw2<Space>*<Space>i<Esc>", cost = 1.0 } },
    }), function(scratch_buf)
      local header_render = require("vimficiency.explore.render.header")
      local insert_helpers = require("vimficiency.explore.insert_helpers")
      local list_render = require("vimficiency.explore.render.list")
      local view = require("vimficiency.explore.registry").current()

      view.state.phase = { kind = "Insert", edit_index = 0 }
      view.pending = {
        target = "2<Space>*<Space>i",
        literal_target = "2 * i",
        row = 0,
        col_start = 0,
      }
      view.on_key_buffer = "ciw2x"
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "2x * i" })
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 2 })

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

test("explore flow: normal deletion snapshot stays transform and suggests insertion", function()
  helpers.silence_notify(function()
    explore_helpers.open_flow("flow-x-then-insert", explore_helpers.fake_result({
      lines = { "n" },
      goal_lines = { "m" },
      end_col = 0,
      user_seq = "sm<Esc>",
      optimal_results = { { seq = "sm<Esc>", cost = 1.0 } },
    }), function(scratch_buf)
      local view = widen_recommendations()
      first_recommendation_in_phase("Transform", function(rec)
        return rec.text == "x"
      end)
      local handlers = require("vimficiency.explore.handlers")
      view.on_key_buffer = "x"
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "" })

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

test("explore flow: bad insert entry snaps back with plan warning", function()
  with_notify_capture(function(notices)
    explore_helpers.open_flow("flow-bad-insert-entry", explore_helpers.fake_result({
      lines = { "ab cde" },
      goal_lines = { "ab de" },
      end_col = 3,
      user_seq = "wxa<Esc>",
      optimal_results = { { seq = "wx", cost = 2.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local view = require("vimficiency.explore.registry").current()

      feed("w")
      explore_helpers.trigger_cursor_moved(scratch_buf)
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "ab de" })
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 3 })
      view.on_key_buffer = "x"
      handlers.on_buffer_changed()
      assert_true(explore.status().is_completed, "precondition: x completed the session")

      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 4 })
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
    explore_helpers.open_flow("flow-fed-append-restore", explore_helpers.fake_result({
      lines = { "ab cde" },
      goal_lines = { "ab de" },
      end_col = 3,
      user_seq = "wxa<Esc>",
      optimal_results = { { seq = "wx", cost = 2.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local view = require("vimficiency.explore.registry").current()

      feed("w")
      explore_helpers.trigger_cursor_moved(scratch_buf)
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "ab de" })
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 3 })
      view.on_key_buffer = "x"
      handlers.on_buffer_changed()
      assert_true(explore.status().is_completed, "precondition: x completed the session")

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
    explore_helpers.open_flow("flow-fed-wx-a-restore", explore_helpers.fake_result({
      lines = { "ab cde" },
      goal_lines = { "ab de" },
      end_col = 3,
      user_seq = "wxa<Esc>",
      optimal_results = { { seq = "wx", cost = 2.0 } },
    }), function(scratch_buf)
      local view = require("vimficiency.explore.registry").current()

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
    explore_helpers.open_flow("flow-bad-insert-mode-restore", explore_helpers.fake_result({
      lines = { "ab cde" },
      goal_lines = { "ab de" },
      end_col = 3,
      user_seq = "wxa<Esc>",
      optimal_results = { { seq = "wx", cost = 2.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local state_mod = require("vimficiency.explore.state")
      local view = require("vimficiency.explore.registry").current()

      feed("w")
      explore_helpers.trigger_cursor_moved(scratch_buf)
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "ab de" })
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 3 })
      view.on_key_buffer = "x"
      handlers.on_buffer_changed()
      assert_true(explore.status().is_completed, "precondition: x completed the session")

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
        vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 4 })
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
    explore_helpers.open_flow("flow-overdelete", explore_helpers.fake_result({
      start_col = 2,
      lines = { "abc", "def" },
      goal_lines = { "ab", "def" },
      end_col = 1,
      user_seq = "x",
      optimal_results = { { seq = "x", cost = 1.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local view = require("vimficiency.explore.registry").current()
      assert_eq(explore.status().phase.kind, "Transform")

      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "def" })
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 0 })
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

test("explore flow: operator-pending duplicate keys are reduced before snapshot", function()
  helpers.silence_notify(function()
    explore_helpers.open_flow("flow-diw-dedup", explore_helpers.fake_result({
      lines = { "word" },
      goal_lines = { "" },
      end_col = 0,
      user_seq = "diw",
      optimal_results = { { seq = "diw", cost = 1.0 } },
    }), function(scratch_buf)
      local handlers = require("vimficiency.explore.handlers")
      local view = require("vimficiency.explore.registry").current()
      view.on_key_events = {
        key_event("n", "d"),
        key_event("no", "i"),
        key_event("n", "i"),
        key_event("n", "w"),
      }
      vim.api.nvim_buf_set_lines(scratch_buf, 0, -1, false, { "" })

      handlers.on_buffer_changed()

      local st = explore.status()
      assert_eq(st.seq, "diw")
      assert_eq(view.on_key_buffer, "")
      assert_eq(#view.on_key_events, 0)
    end)
  end)
end)

test("explore flow: live cursor move leaves transform range despite buffered keys", function()
  helpers.silence_notify(function()
    explore_helpers.open_flow("flow-ciw-precursor", explore_helpers.fake_result({
      start_col = 1,
      lines = { "abc" },
      goal_lines = { "aBc" },
      end_col = 1,
      user_seq = "rB",
      optimal_results = { { seq = "rB", cost = 1.0 } },
    }), function()
      local view = require("vimficiency.explore.registry").current()
      local handlers = require("vimficiency.explore.handlers")
      view.on_key_buffer = "ci"
      vim.api.nvim_win_set_cursor(view.scratch.win, { 1, 0 })

      handlers.on_cursor_moved()

      local st = explore.status()
      assert_eq(st.phase.kind, "Navigate")
      assert_eq(view.on_key_buffer, "")
      assert_eq(st.scratch_cursor.col, 0)
    end)
  end)
end)

test("explore flow: insert mismatch reverts scratch buffer", function()
  helpers.silence_notify(function()
    explore_helpers.open_flow("flow-insert-bad", explore_helpers.fake_result(), function(scratch_buf)
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
