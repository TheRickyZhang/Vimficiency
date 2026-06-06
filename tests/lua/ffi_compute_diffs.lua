local ffi_lib = require("vimficiency.ffi")

test("ffi._parse_diff_regions: parses the 8-field wire format", function()
  local US = "\x1F"
  local payload = table.concat({
    table.concat({ "0", "4", "0", "7", "0", "4", "0", "7" }, US),
    table.concat({ "1", "0", "1", "0", "1", "0", "2", "3" }, US),
  }, "\n")

  local regions = ffi_lib._parse_diff_regions(payload)
  assert_eq(#regions, 2, "two regions")

  assert_eq(regions[1].init.begin_row, 0)
  assert_eq(regions[1].init.begin_col, 4)
  assert_eq(regions[1].init.end_row, 0)
  assert_eq(regions[1].init.end_col, 7)
  assert_eq(regions[1].goal.end_col, 7)

  assert_eq(regions[2].goal.end_row, 2)
  assert_eq(regions[2].goal.end_col, 3)
end)

test("ffi.compute_diffs: single-line substitution maps both sides", function()
  local regions = ffi_lib.compute_diffs({ "aaa bbb" }, { "aaa ccc" })
  assert_eq(#regions, 1, "one contiguous change region")

  local r = regions[1]
  -- "bbb" -> "ccc" at col 4; both spans are [4, 7) on row 0.
  assert_eq(r.init.begin_row, 0); assert_eq(r.init.begin_col, 4)
  assert_eq(r.init.end_row, 0);   assert_eq(r.init.end_col, 7)
  assert_eq(r.goal.begin_row, 0); assert_eq(r.goal.begin_col, 4)
  assert_eq(r.goal.end_row, 0);   assert_eq(r.goal.end_col, 7)
end)

test("ffi.compute_diffs: pure insertion has an empty init span", function()
  local regions = ffi_lib.compute_diffs({ "ac" }, { "abc" })
  assert_eq(#regions, 1)

  local r = regions[1]
  -- Insert "b" at col 1: init span empty, goal span [1, 2).
  assert_eq(r.init.begin_row, r.init.end_row)
  assert_eq(r.init.begin_col, r.init.end_col, "init span is empty for a pure insertion")
  assert_eq(r.goal.begin_col, 1)
  assert_eq(r.goal.end_col, 2)
end)

test("ffi.compute_diffs: identical buffers yield no regions", function()
  local regions = ffi_lib.compute_diffs({ "same", "lines" }, { "same", "lines" })
  assert_eq(#regions, 0)
end)
