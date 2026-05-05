local base = require("_helpers")

local M = {}

local function explore()
  return require("vimficiency.explore")
end

function M.find_window_by_name(expected_name, wins)
  wins = wins or vim.api.nvim_tabpage_list_wins(vim.api.nvim_get_current_tabpage())
  for _, win in ipairs(wins) do
    local buf = vim.api.nvim_win_get_buf(win)
    if vim.api.nvim_buf_get_name(buf) == expected_name then
      return win, buf
    end
  end
  return nil, nil
end

function M.find_windows_by_prefix(prefix, wins)
  wins = wins or vim.api.nvim_tabpage_list_wins(vim.api.nvim_get_current_tabpage())
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

function M.trigger_cursor_moved(buf)
  vim.api.nvim_exec_autocmds("CursorMoved", { buffer = buf, modeline = false })
end

function M.status_text()
  return vim.inspect(explore().status())
end

function M.wait_for(label, pred)
  local ok = vim.wait(1000, function()
    local passed, result = pcall(pred)
    return passed and result
  end, 10)
  assert_true(ok, label .. "\nstatus=" .. M.status_text())
end

function M.open_flow(label, result, fn)
  local explore_mod = explore()
  local scratch_tab
  local ok, err = pcall(function()
    base.new_buf(result.lines)
    assert_true(explore_mod.open(label, result), "explore.open should succeed")
    scratch_tab = vim.api.nvim_get_current_tabpage()

    local scratch_win, scratch_buf = M.find_window_by_name("vimficiency://explore/" .. label)
    assert_true(scratch_win ~= nil, "missing scratch window")
    assert_true(scratch_buf ~= nil, "missing scratch buffer")
    vim.api.nvim_set_current_win(scratch_win)

    fn(scratch_buf)
  end)

  pcall(explore_mod.cancel)
  if scratch_tab then
    vim.wait(200, function()
      return not vim.api.nvim_tabpage_is_valid(scratch_tab)
    end, 10)
  end
  pcall(vim.cmd, "silent! tabonly")

  if not ok then error(err, 0) end
end

function M.fake_result(overrides)
  return base.fake_result(vim.tbl_extend("force", {
    lines = { "abc" },
    goal_lines = { "aBc" },
    end_row = 0,
    end_col = 1,
    user_seq = "lsB<Esc>",
    optimal_results = { { seq = "lsB<Esc>", cost = 1.0 } },
  }, overrides or {}))
end

return M
