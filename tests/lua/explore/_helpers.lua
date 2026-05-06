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

---Returns the header virt_lines as plain strings (one per virtual row),
---or nil if no header extmark is attached.
---@param scratch_buf integer
---@return string[]|nil
function M.header_virt_strings(scratch_buf)
  local ns = require("vimficiency.explore.render.header").header_ns
  local marks = vim.api.nvim_buf_get_extmarks(scratch_buf, ns, 0, -1, { details = true })
  for _, mark in ipairs(marks) do
    local details = mark[4]
    if details and details.virt_lines then
      local out = {}
      for _, vline in ipairs(details.virt_lines) do
        local parts = {}
        for _, chunk in ipairs(vline) do parts[#parts + 1] = chunk[1] end
        out[#out + 1] = table.concat(parts)
      end
      return out
    end
  end
  return nil
end

---Returns the contiguous block of strings rendered for the column with
---the given title — including the title row itself, then content lines,
---trimmed of trailing column padding. Useful for asserting a column's
---visible contents without depending on the surrounding columns' widths.
---@param scratch_buf integer
---@param title string
---@return string[]|nil
function M.header_column_lines(scratch_buf, title)
  local strings = M.header_virt_strings(scratch_buf)
  if not strings then return nil end

  local title_row_idx
  for i, line in ipairs(strings) do
    if line:find(title, 1, true) then title_row_idx = i; break end
  end
  if not title_row_idx then return nil end

  local title_row = strings[title_row_idx]
  local start_byte = assert(title_row:find(title, 1, true),
    "title row does not contain the title we just matched")

  local next_byte
  for _, t in ipairs({ "Explored", "User typed", "Optimal 1", "Optimal 2",
                       "Optimal 3", "Optimal 4", "Optimal 5" }) do
    if t ~= title then
      local pos = title_row:find(t, start_byte + #title, true)
      if pos and (not next_byte or pos < next_byte) then
        next_byte = pos
      end
    end
  end
  next_byte = next_byte or (#title_row + 1)

  local out = {}
  for i = title_row_idx, #strings do
    local slice = strings[i]:sub(start_byte, next_byte - 1)
    out[#out + 1] = (slice:gsub("%s+$", ""))
  end
  return out
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

function M.current_view()
  return require("vimficiency.explore.registry").current()
end

function M.set_scratch(lines, cursor)
  local view = M.current_view()
  vim.api.nvim_buf_set_lines(view.scratch.buf, 0, -1, false, lines)
  if cursor then vim.api.nvim_win_set_cursor(view.scratch.win, cursor) end
  return view
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

function M.result(name, overrides)
  return base.result_scenario(name, overrides)
end

function M.open_scenario_flow(label, scenario_name, overrides, fn)
  if type(overrides) == "function" then
    fn = overrides
    overrides = nil
  end
  return M.open_flow(label, M.result(scenario_name, overrides), fn)
end

function M.fake_result(overrides)
  return M.result("basic_insert", overrides)
end

return M
