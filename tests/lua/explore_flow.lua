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

local function feed(keys)
  local termkeys = vim.api.nvim_replace_termcodes(keys, true, false, true)
  vim.api.nvim_feedkeys(termkeys, "xt", false)
end

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

local function first_recommendation(kind, pred)
  local st = explore.status()
  assert_true(st ~= nil, "explore status should be available")
  for _, rec in ipairs(st.recommendations) do
    if rec.kind == kind and (not pred or pred(rec)) then
      return rec
    end
  end
  error("no " .. kind .. " recommendation matched\nstatus=" .. status_text(), 2)
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
  if not (rec.typed_text and rec.typed_text ~= "") then return false end
  local first = rec.text:sub(1, 1)
  return first == "C" or first == "c" or first == "s"
    or first == "i" or first == "a" or first == "A"
    or first == "I" or first == "o" or first == "O"
end

local function move_to_first_edit_target(scratch_buf)
  local target = explore.status()
  local motion = first_recommendation("movement", function(rec)
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
      local motion = first_recommendation("movement")
      feed(motion.text)
      trigger_cursor_moved(scratch_buf)

      wait_for("motion-only goal should complete", function()
        local st = explore.status()
        return st
          and st.phase.kind == "Completed"
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

      local edit = first_recommendation("edit", enters_insert_mode)
      feed(edit.text .. edit.typed_text .. "<Esc>")

      wait_for("matching insert should reach the goal buffer", function()
        local st = explore.status()
        return st
          and st.phase.kind == "Completed"
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

      local edit = first_recommendation("edit", enters_insert_mode)
      local wrong = edit.typed_text == "X" and "Y" or "X"
      feed(edit.text .. wrong .. "<Esc>")

      wait_for("mismatched insert should revert to the current fencepost", function()
        local st = explore.status()
        return st
          and st.phase.kind == "ApproachEdit"
          and vim.deep_equal(st.session_lines, { "abc" })
          and vim.deep_equal(st.scratch_lines, { "abc" })
          and st.pending == nil
      end)
    end)
  end)
end)
