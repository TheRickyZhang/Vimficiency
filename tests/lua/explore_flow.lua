local helpers = require("_helpers")
local explore = require("vimficiency.explore")

local function find_window_by_name(expected_name)
  for _, win in ipairs(vim.api.nvim_tabpage_list_wins(vim.api.nvim_get_current_tabpage())) do
    local buf = vim.api.nvim_win_get_buf(win)
    if vim.api.nvim_buf_get_name(buf) == expected_name then
      return win, buf
    end
  end
  return nil, nil
end

local feed = helpers.feed

local function trigger_cursor_moved(buf)
  vim.api.nvim_exec_autocmds("CursorMoved", { buffer = buf, modeline = false })
end

local function status_text()
  return vim.inspect(explore.status())
end

local function wait_for(label, pred)
  local ok = vim.wait(1000, function()
    local passed, result = pcall(pred)
    return passed and result
  end, 10)
  assert_true(ok, label .. "\nstatus=" .. status_text())
end

local function open_flow(label, result, fn)
  local scratch_tab
  local ok, err = pcall(function()
    helpers.new_buf(result.lines)
    assert_true(explore.open(label, result), "explore.open should succeed")
    scratch_tab = vim.api.nvim_get_current_tabpage()

    local scratch_win = find_window_by_name("vimficiency://explore/" .. label)
    assert_true(scratch_win ~= nil, "missing scratch window")
    vim.api.nvim_set_current_win(scratch_win)

    fn(scratch_buf)
  end)

  pcall(explore.cancel)
  if scratch_tab then
    vim.wait(200, function()
      return not vim.api.nvim_tabpage_is_valid(scratch_tab)
    end, 10)
  end
  pcall(vim.cmd, "silent! tabonly")

  if not ok then error(err, 0) end
end

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
        "\nstatus=" .. status_text(), 2)
end

local function fake_result(overrides)
  return helpers.fake_result(vim.tbl_extend("force", {
    lines = { "abc" },
    goal_lines = { "aBc" },
    end_row = 0,
    end_col = 1,
    user_seq = "lsB<Esc>",
    optimal_results = { { seq = "lsB<Esc>", cost = 1.0 } },
  }, overrides or {}))
end

local function enters_insert_mode(rec)
  -- The typed-text content moved into the Insert-phase rec; here we only
  -- check the structural prefix's first char.
  local first = rec.text:sub(1, 1)
  return first == "C" or first == "c" or first == "s"
    or first == "i" or first == "a" or first == "A"
    or first == "I" or first == "o" or first == "O"
end

local function move_to_first_edit_target(scratch_buf)
  local target = explore.status()
  local motion = first_recommendation_in_phase("Navigate", function(rec)
    return target.target_range
      and rec.landing.row == target.target_range.begin_pos.row
      and rec.landing.col == target.target_range.begin_pos.col
  end)
  feed(motion.text)
  trigger_cursor_moved(scratch_buf)
  wait_for("motion recommendation should update FFI cursor", function()
    local st = explore.status()
    return st
      and st.cursor.row == motion.landing.row
      and st.cursor.col == motion.landing.col
      and st.accepted_seq ~= ""
  end)
end

test("explore flow: natural motion updates cursor and completes motion-only goal", function()
  helpers.silence_notify(function()
    open_flow("flow-motion", fake_result({
      lines = { "foo bar" },
      goal_lines = { "foo bar" },
      end_col = 4,
      user_seq = "w",
      optimal_results = { { seq = "w", cost = 1.0 } },
    }), function(scratch_buf)
      local motion = first_recommendation_in_phase("Navigate")
      feed(motion.text)
      trigger_cursor_moved(scratch_buf)

      wait_for("motion-only goal should complete", function()
        local st = explore.status()
        return st
          and st.is_completed
          and st.cursor.row == motion.landing.row
          and st.cursor.col == motion.landing.col
          and st.accepted_seq ~= ""
      end)
      assert_eq(explore.status().scratch_lines, { "foo bar" })
    end)
  end)
end)

test("explore flow: insert recommendation accepts matching scratch edit", function()
  helpers.silence_notify(function()
    open_flow("flow-insert-ok", fake_result(), function(scratch_buf)
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
      wait_for("matching insert should reach the goal buffer", function()
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

test("explore flow: insert mismatch reverts scratch buffer", function()
  helpers.silence_notify(function()
    open_flow("flow-insert-bad", fake_result(), function(scratch_buf)
      move_to_first_edit_target(scratch_buf)

      -- Feed structural + WRONG typed + <Esc>. Same shape as the matching
      -- variant, but with a typed continuation that doesn't reach goal.
      local edit = first_recommendation_in_phase("Transform", enters_insert_mode)
      feed(edit.text .. "X<Esc>")

      wait_for("mismatched insert should revert to the current fencepost", function()
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
