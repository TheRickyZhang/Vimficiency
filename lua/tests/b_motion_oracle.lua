-- lua/tests/b_motion_oracle.lua
--
-- Neovim ground truth generator for b-motion edit tests (db, cb, dB, cB)
--
-- Usage:
--   nvim --headless -u NONE -l lua/tests/b_motion_oracle.lua
--
-- This script runs edit operations in actual Neovim and outputs the results
-- in a format that can be compared against our C++ implementation.

local function escape_string(s)
  return (s:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n"))
end

local function lines_to_cpp_array(lines)
  local parts = {}
  for _, line in ipairs(lines) do
    table.insert(parts, '"' .. escape_string(line) .. '"')
  end
  return "{" .. table.concat(parts, ", ") .. "}"
end

-- Determine if a command enters insert mode based on Vim semantics
local function enters_insert_mode(keys)
  -- cb, cB, cw, cW, ce, cE, c0, c$, cc, C, s, S all enter insert mode
  -- d operations stay in normal mode
  return keys:match("^c") or keys:match("^s") or keys:match("^S")
end

local function run_test(test_name, initial_lines, start_row, start_col, keys)
  -- Create a scratch buffer
  local buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_set_current_buf(buf)

  -- Set the initial content
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, initial_lines)

  -- Position cursor (Neovim uses 1-indexed rows, 0-indexed cols)
  vim.api.nvim_win_set_cursor(0, {start_row + 1, start_col})

  -- Execute the keys in normal mode
  -- Use feedkeys with 'x' flag to execute immediately, which preserves mode
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes(keys, true, false, true), "x", false)

  -- Get final state
  local final_lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
  local final_cursor = vim.api.nvim_win_get_cursor(0)
  local final_row = final_cursor[1] - 1  -- Convert back to 0-indexed
  local final_col = final_cursor[2]

  -- Determine mode: feedkeys 'x' should preserve it, but in headless it's unreliable
  -- Use semantic knowledge instead
  local is_insert = enters_insert_mode(keys)

  -- Return to normal mode if in insert (for buffer cleanup)
  vim.cmd("stopinsert")

  -- Delete buffer
  vim.api.nvim_buf_delete(buf, {force = true})

  return {
    name = test_name,
    initial = initial_lines,
    start_pos = {start_row, start_col},
    keys = keys,
    final_lines = final_lines,
    final_pos = {final_row, final_col},
    is_insert = is_insert,
  }
end

local function print_result(r)
  local mode_str = r.is_insert and "Mode::Insert" or "Mode::Normal"
  print(string.format("// %s", r.name))
  print(string.format("// Initial: %s at (%d, %d)", lines_to_cpp_array(r.initial), r.start_pos[1], r.start_pos[2]))
  print(string.format("// Keys: %s", r.keys))
  print(string.format("// Result: %s at (%d, %d) %s",
    lines_to_cpp_array(r.final_lines), r.final_pos[1], r.final_pos[2], mode_str))
  print("")
end

local function print_cpp_test(r, test_prefix)
  local mode_str = r.is_insert and "Mode::Insert" or "Mode::Normal"
  local func_name = test_prefix .. "_" .. r.name:gsub("[^%w]+", "_")

  print(string.format("TEST_F(EditTest, %s) {", func_name))
  print(string.format("  auto r = applyNormalEdit(%s, {%d, %d}, \"%s\");",
    lines_to_cpp_array(r.initial), r.start_pos[1], r.start_pos[2], r.keys))
  print(string.format("  expectState(r, %s, %d, %d, %s, \"%s\");",
    lines_to_cpp_array(r.final_lines), r.final_pos[1], r.final_pos[2], mode_str, r.name))
  print("}")
  print("")
end

-- ============================================================================
-- Test Cases: db (delete backward to word start)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= db TESTS (delete backward to word start)")
print("=" .. string.rep("=", 70))
print("")

local db_tests = {
  -- Within same line
  run_test("db_within_line", {"one two three"}, 0, 8, "db"),
  run_test("db_at_word_start", {"one two three"}, 0, 4, "db"),
  run_test("db_in_middle_of_word", {"one two three"}, 0, 5, "db"),
  run_test("db_at_second_char", {"one two"}, 0, 5, "db"),

  -- Cross-line cases (the key ones!)
  run_test("db_cross_line_at_col0", {"ab", "cd"}, 1, 0, "db"),
  run_test("db_cross_line_at_col1", {"ab", "cd"}, 1, 1, "db"),
  run_test("db_cross_multiline", {"aa", "bb", "cc"}, 2, 0, "db"),

  -- Edge cases
  run_test("db_single_word_line", {"word"}, 0, 2, "db"),
  run_test("db_with_punctuation", {"foo.bar baz"}, 0, 8, "db"),
  run_test("db_with_spaces", {"  word"}, 0, 4, "db"),
  run_test("db_trailing_spaces", {"word  "}, 0, 5, "db"),
}

for _, r in ipairs(db_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for db:")
print("")
for _, r in ipairs(db_tests) do
  print_cpp_test(r, "Db_Neovim")
end

-- ============================================================================
-- Test Cases: cb (change backward to word start)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= cb TESTS (change backward to word start)")
print("=" .. string.rep("=", 70))
print("")

local cb_tests = {
  -- Within same line
  run_test("cb_within_line", {"one two three"}, 0, 8, "cb"),
  run_test("cb_at_word_start", {"one two three"}, 0, 4, "cb"),
  run_test("cb_in_middle_of_word", {"one two three"}, 0, 5, "cb"),

  -- Cross-line cases (the key ones!)
  run_test("cb_cross_line_at_col0", {"ab", "cd"}, 1, 0, "cb"),
  run_test("cb_cross_line_at_col1", {"ab", "cd"}, 1, 1, "cb"),
  run_test("cb_cross_multiline", {"aa", "bb", "cc"}, 2, 0, "cb"),

  -- Edge cases
  run_test("cb_single_word_line", {"word"}, 0, 2, "cb"),
  run_test("cb_with_punctuation", {"foo.bar baz"}, 0, 8, "cb"),
}

for _, r in ipairs(cb_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for cb:")
print("")
for _, r in ipairs(cb_tests) do
  print_cpp_test(r, "Cb_Neovim")
end

-- ============================================================================
-- Test Cases: dB (delete backward to WORD start)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= dB TESTS (delete backward to WORD start)")
print("=" .. string.rep("=", 70))
print("")

local dB_tests = {
  -- Within same line
  run_test("dB_within_line", {"foo.bar baz.qux"}, 0, 8, "dB"),
  run_test("dB_at_WORD_start", {"foo.bar baz"}, 0, 8, "dB"),

  -- Cross-line cases
  run_test("dB_cross_line_at_col0", {"ab", "cd"}, 1, 0, "dB"),
  run_test("dB_cross_line_at_col1", {"ab", "cd"}, 1, 1, "dB"),

  -- Punctuation handling
  run_test("dB_with_punctuation", {"foo.bar baz"}, 0, 4, "dB"),
}

for _, r in ipairs(dB_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for dB:")
print("")
for _, r in ipairs(dB_tests) do
  print_cpp_test(r, "DB_Neovim")
end

-- ============================================================================
-- Test Cases: cB (change backward to WORD start)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= cB TESTS (change backward to WORD start)")
print("=" .. string.rep("=", 70))
print("")

local cB_tests = {
  -- Within same line
  run_test("cB_within_line", {"foo.bar baz.qux"}, 0, 8, "cB"),

  -- Cross-line cases
  run_test("cB_cross_line_at_col0", {"ab", "cd"}, 1, 0, "cB"),
  run_test("cB_cross_line_at_col1", {"ab", "cd"}, 1, 1, "cB"),
}

for _, r in ipairs(cB_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for cB:")
print("")
for _, r in ipairs(cB_tests) do
  print_cpp_test(r, "CB_Neovim")
end

-- ============================================================================
-- Test w/W edit operations (dw, cw, dW, cW)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= dw TESTS (delete forward to word start)")
print("=" .. string.rep("=", 70))
print("")

local dw_tests = {
  -- Within same line
  run_test("dw_within_line", {"one two three"}, 0, 0, "dw"),
  run_test("dw_at_word_end", {"one two three"}, 0, 2, "dw"),
  run_test("dw_in_middle_of_word", {"one two three"}, 0, 1, "dw"),
  run_test("dw_on_whitespace", {"one  two"}, 0, 3, "dw"),

  -- Cross-line cases
  run_test("dw_cross_line_last_word", {"ab", "cd"}, 0, 0, "dw"),
  run_test("dw_cross_line_at_end", {"ab", "cd"}, 0, 1, "dw"),

  -- Edge cases
  run_test("dw_single_word", {"word"}, 0, 0, "dw"),
  run_test("dw_with_punctuation", {"foo.bar baz"}, 0, 0, "dw"),
  run_test("dw_empty_line_below", {"ab", "", "cd"}, 0, 0, "dw"),
}

for _, r in ipairs(dw_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for dw:")
print("")
for _, r in ipairs(dw_tests) do
  print_cpp_test(r, "Dw_Neovim")
end

-- ============================================================================
-- Test cw operations
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= cw TESTS (change forward - special: acts like ce on word)")
print("=" .. string.rep("=", 70))
print("")

local cw_tests = {
  -- Within same line (cw on word acts like ce)
  run_test("cw_within_line", {"one two three"}, 0, 0, "cw"),
  run_test("cw_at_word_end", {"one two three"}, 0, 2, "cw"),
  run_test("cw_on_whitespace", {"one  two"}, 0, 3, "cw"),

  -- Cross-line cases
  run_test("cw_cross_line_last_word", {"ab", "cd"}, 0, 0, "cw"),
  run_test("cw_at_line_end", {"ab", "cd"}, 0, 1, "cw"),

  -- Edge cases
  run_test("cw_single_word", {"word"}, 0, 0, "cw"),
  run_test("cw_with_punctuation", {"foo.bar baz"}, 0, 0, "cw"),
}

for _, r in ipairs(cw_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for cw:")
print("")
for _, r in ipairs(cw_tests) do
  print_cpp_test(r, "Cw_Neovim")
end

-- ============================================================================
-- Test e/E edit operations (de, ce, dE, cE)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= de TESTS (delete forward to word end - inclusive)")
print("=" .. string.rep("=", 70))
print("")

local de_tests = {
  -- Within same line
  run_test("de_within_line", {"one two three"}, 0, 0, "de"),
  run_test("de_at_word_end", {"one two three"}, 0, 2, "de"),
  run_test("de_in_middle", {"one two three"}, 0, 1, "de"),

  -- Cross-line cases
  run_test("de_cross_line_at_end", {"ab", "cd"}, 0, 1, "de"),
  run_test("de_last_char_of_buffer", {"ab"}, 0, 1, "de"),

  -- Edge cases
  run_test("de_single_char", {"a"}, 0, 0, "de"),
  run_test("de_with_punctuation", {"foo.bar baz"}, 0, 0, "de"),
}

for _, r in ipairs(de_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for de:")
print("")
for _, r in ipairs(de_tests) do
  print_cpp_test(r, "De_Neovim")
end

-- ============================================================================
-- Test ce operations
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= ce TESTS (change forward to word end)")
print("=" .. string.rep("=", 70))
print("")

local ce_tests = {
  run_test("ce_within_line", {"one two three"}, 0, 0, "ce"),
  run_test("ce_at_word_end", {"one two three"}, 0, 2, "ce"),
  run_test("ce_cross_line_at_end", {"ab", "cd"}, 0, 1, "ce"),
  run_test("ce_single_char", {"a"}, 0, 0, "ce"),
}

for _, r in ipairs(ce_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for ce:")
print("")
for _, r in ipairs(ce_tests) do
  print_cpp_test(r, "Ce_Neovim")
end

-- ============================================================================
-- Test ge/gE edit operations (dge, cge, dgE, cgE)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= dge TESTS (delete backward to word end)")
print("=" .. string.rep("=", 70))
print("")

local dge_tests = {
  -- Within same line
  run_test("dge_within_line", {"one two three"}, 0, 8, "dge"),
  run_test("dge_at_word_start", {"one two three"}, 0, 4, "dge"),
  run_test("dge_in_middle", {"one two three"}, 0, 5, "dge"),

  -- Cross-line cases
  run_test("dge_cross_line_at_col0", {"ab", "cd"}, 1, 0, "dge"),
  run_test("dge_cross_line_at_col1", {"ab", "cd"}, 1, 1, "dge"),
  run_test("dge_cross_multiline", {"aa", "bb", "cc"}, 2, 0, "dge"),

  -- Edge cases
  run_test("dge_with_punctuation", {"foo.bar baz"}, 0, 8, "dge"),
}

for _, r in ipairs(dge_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for dge:")
print("")
for _, r in ipairs(dge_tests) do
  print_cpp_test(r, "Dge_Neovim")
end

-- ============================================================================
-- Test cge operations
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= cge TESTS (change backward to word end)")
print("=" .. string.rep("=", 70))
print("")

local cge_tests = {
  run_test("cge_within_line", {"one two three"}, 0, 8, "cge"),
  run_test("cge_cross_line_at_col0", {"ab", "cd"}, 1, 0, "cge"),
  run_test("cge_cross_line_at_col1", {"ab", "cd"}, 1, 1, "cge"),
}

for _, r in ipairs(cge_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for cge:")
print("")
for _, r in ipairs(cge_tests) do
  print_cpp_test(r, "Cge_Neovim")
end

-- ============================================================================
-- Test WORD motions (W, E, B, gE)
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= dW TESTS (delete forward to WORD start)")
print("=" .. string.rep("=", 70))
print("")

local dW_tests = {
  run_test("dW_within_line", {"foo.bar baz.qux"}, 0, 0, "dW"),
  run_test("dW_cross_line", {"foo.bar", "baz"}, 0, 0, "dW"),
  run_test("dW_with_punctuation", {"foo.bar baz"}, 0, 4, "dW"),
}

for _, r in ipairs(dW_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for dW:")
print("")
for _, r in ipairs(dW_tests) do
  print_cpp_test(r, "DW_Neovim")
end

print("=" .. string.rep("=", 70))
print("= dE TESTS (delete forward to WORD end)")
print("=" .. string.rep("=", 70))
print("")

local dE_tests = {
  run_test("dE_within_line", {"foo.bar baz.qux"}, 0, 0, "dE"),
  run_test("dE_cross_line", {"foo.bar", "baz"}, 0, 4, "dE"),
}

for _, r in ipairs(dE_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for dE:")
print("")
for _, r in ipairs(dE_tests) do
  print_cpp_test(r, "DE_Neovim")
end

print("=" .. string.rep("=", 70))
print("= dgE TESTS (delete backward to WORD end)")
print("=" .. string.rep("=", 70))
print("")

local dgE_tests = {
  run_test("dgE_within_line", {"foo.bar baz.qux"}, 0, 8, "dgE"),
  run_test("dgE_cross_line_at_col0", {"foo.bar", "baz"}, 1, 0, "dgE"),
}

for _, r in ipairs(dgE_tests) do
  print_result(r)
end

print("")
print("// Generated C++ tests for dgE:")
print("")
for _, r in ipairs(dgE_tests) do
  print_cpp_test(r, "DgE_Neovim")
end

-- ============================================================================
-- Debug: Test motion landing positions for symmetry analysis
-- ============================================================================

print("=" .. string.rep("=", 70))
print("= DEBUG: Motion landing positions for symmetry analysis")
print("=" .. string.rep("=", 70))
print("")

local function test_motion(motion, lines, row, col)
  local buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_set_current_buf(buf)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.api.nvim_win_set_cursor(0, {row + 1, col})

  local before = vim.api.nvim_win_get_cursor(0)
  vim.cmd("normal! " .. motion)
  local after = vim.api.nvim_win_get_cursor(0)

  print(string.format("%s from (%d,%d) -> (%d,%d) on %s",
    motion, before[1] - 1, before[2], after[1] - 1, after[2], vim.inspect(lines)))
  vim.api.nvim_buf_delete(buf, {force = true})
end

-- Test symmetry: w/ge should be opposites (both go to word boundaries)
print("-- w/ge symmetry test on {'one two three'}")
test_motion("w", {"one two three"}, 0, 0)   -- from 'o' -> ?
test_motion("ge", {"one two three"}, 0, 4)  -- from 't' (start of two) -> ?

print("")
print("-- e/b symmetry test on {'one two three'}")
test_motion("e", {"one two three"}, 0, 0)   -- from 'o' -> ?
test_motion("b", {"one two three"}, 0, 6)   -- from end of 'two' -> ?

print("")
print("-- Cross-line tests")
test_motion("w", {"ab", "cd"}, 0, 0)
test_motion("ge", {"ab", "cd"}, 1, 0)
test_motion("e", {"ab", "cd"}, 0, 0)
test_motion("b", {"ab", "cd"}, 1, 0)

-- Exit
vim.cmd("qa!")
