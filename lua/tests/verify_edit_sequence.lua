-- lua/tests/verify_edit_sequence.lua
--
-- Verify edit sequences from EditOptimizer against Neovim ground truth
--
-- Usage:
--   nvim --headless -u NONE -l lua/tests/verify_edit_sequence.lua

local function escape_string(s)
  return (s:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n"))
end

local function lines_to_string(lines)
  local parts = {}
  for _, line in ipairs(lines) do
    table.insert(parts, '"' .. escape_string(line) .. '"')
  end
  return "{" .. table.concat(parts, ", ") .. "}"
end

local function run_sequence(initial_lines, start_row, start_col, keys)
  -- Create a scratch buffer
  local buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_set_current_buf(buf)

  -- Set the initial content
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, initial_lines)

  -- Position cursor (Neovim uses 1-indexed rows, 0-indexed cols)
  vim.api.nvim_win_set_cursor(0, {start_row + 1, start_col})

  -- Execute the keys
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys, true, false, true), "x", false)

  -- Get final state
  local final_lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
  local final_cursor = vim.api.nvim_win_get_cursor(0)
  local final_row = final_cursor[1] - 1
  local final_col = final_cursor[2]
  local mode = vim.api.nvim_get_mode().mode

  -- Return to normal mode
  vim.cmd("stopinsert")
  vim.api.nvim_buf_delete(buf, {force = true})

  return {
    lines = final_lines,
    row = final_row,
    col = final_col,
    mode = mode,
  }
end

-- Test case from EditOptimizer output:
-- Begin: {"aa", "bb", "cc"}
-- End: {"xx", "yy"}
-- Found sequence: decexx\nyy<Esc> starting from position 1 (which is (0,1))
--
-- NOTE: The display output shows "dece xx\nyy<Esc>" with a space between mode segments,
-- but the actual sequence is "decexx\nyy<Esc>" (no space). The space in display is just
-- formatting to show where Normal mode ends and Insert mode begins.

print("=== Verifying EditOptimizer sequence ===")
print("")

local initial = {"aa", "bb", "cc"}
local expected = {"xx", "yy"}

-- Position 1 = (0, 1) = second 'a' in "aa"
local start_row, start_col = 0, 1

-- The found sequence (NO space between 'ce' and 'xx' - that's just display formatting)
local keys = "decexx\nyy" .. vim.api.nvim_replace_termcodes("<Esc>", true, false, true)

print("Initial: " .. lines_to_string(initial))
print("Start pos: (" .. start_row .. ", " .. start_col .. ")")
print("Keys: decexx<CR>yy<Esc>")
print("")

local result = run_sequence(initial, start_row, start_col, keys)

print("Result lines: " .. lines_to_string(result.lines))
print("Result pos: (" .. result.row .. ", " .. result.col .. ")")
print("Mode: " .. result.mode)
print("")

-- Check if it matches expected
local matches = #result.lines == #expected
if matches then
  for i, line in ipairs(result.lines) do
    if line ~= expected[i] then
      matches = false
      break
    end
  end
end

if matches then
  print("SUCCESS: Result matches expected " .. lines_to_string(expected))
else
  print("FAILURE: Expected " .. lines_to_string(expected) .. " but got " .. lines_to_string(result.lines))
end

-- Also trace through step by step
print("")
print("=== Step by step trace ===")

local function trace_step(lines, row, col, keys, description)
  local result = run_sequence(lines, row, col, keys)
  print(string.format("%s: %s -> %s at (%d, %d) mode=%s",
    description, lines_to_string(lines), lines_to_string(result.lines),
    result.row, result.col, result.mode))
  return result.lines, result.row, result.col
end

local lines, row, col = initial, start_row, start_col
print("Start: " .. lines_to_string(lines) .. " at (" .. row .. ", " .. col .. ")")

lines, row, col = trace_step(lines, row, col, "de", "de")
lines, row, col = trace_step(lines, row, col, "ce", "ce")

-- Now in insert mode, type the text
local buf = vim.api.nvim_create_buf(false, true)
vim.api.nvim_set_current_buf(buf)
vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
vim.api.nvim_win_set_cursor(0, {row + 1, col})

-- Type "xx\nyy<Esc>" (no leading space!)
local type_keys = "xx\nyy" .. vim.api.nvim_replace_termcodes("<Esc>", true, false, true)
vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(type_keys, true, false, true), "x", false)

local final_lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
local final_cursor = vim.api.nvim_win_get_cursor(0)
print("After typing 'xx<CR>yy<Esc>': " .. lines_to_string(final_lines) ..
      " at (" .. (final_cursor[1]-1) .. ", " .. final_cursor[2] .. ")")

vim.cmd("qa!")
