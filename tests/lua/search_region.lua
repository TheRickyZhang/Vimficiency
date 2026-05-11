local util = require("vimficiency.util")

test("compute_search_region: uses cursor span when buffers are equal", function()
  local start_row, end_row = util.compute_search_region(
    5,
    7,
    { "a", "b", "c", "d", "e", "f", "g", "h", "i" },
    { "a", "b", "c", "d", "e", "f", "g", "h", "i" },
    2
  )
  assert_eq(start_row, 3)
  assert_eq(end_row, 8)
end)

test("compute_search_region: expands to cover changed lines outside cursor span", function()
  local start_row, end_row = util.compute_search_region(
    10,
    12,
    { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" },
    { "0", "1", "changed", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" },
    1
  )
  assert_eq(start_row, 1)
  assert_eq(end_row, 12)
end)

test("compute_search_region: clamps against the larger buffer after line changes", function()
  local start_row, end_row = util.compute_search_region(
    0,
    0,
    { "a" },
    { "a", "b", "c" },
    2
  )
  assert_eq(start_row, 0)
  assert_eq(end_row, 2)
end)

test("compute_search_region: preserves trailing empty buffer lines", function()
  local start_row, end_row = util.compute_search_region(
    0,
    0,
    { "a" },
    { "a", "" },
    0
  )
  assert_eq(start_row, 0)
  assert_eq(end_row, 1)
end)

test("compute_search_region: rejects empty line arrays", function()
  assert_error(function()
    util.compute_search_region(0, 0, {}, { "" }, 0)
  end, "at least one line")
end)

test("compute_search_region: rejects newline bytes inside a line", function()
  assert_error(function()
    util.compute_search_region(0, 0, { "a\nb" }, { "a" }, 0)
  end, "newline byte")
end)

test("compute_search_region: rejects NUL bytes inside a line", function()
  assert_error(function()
    util.compute_search_region(0, 0, { "a" .. string.char(0) .. "b" }, { "a" }, 0)
  end, "NUL byte")
end)
