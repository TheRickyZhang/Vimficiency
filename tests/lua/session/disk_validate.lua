-- disk.validate must reject saved results missing cursor fields, including the
-- end_row/end_col that the display path (ffi/display.lua) asserts on.

local disk = require("vimficiency.session.disk")

local function base()
  return {
    lines = { "x" },
    start_row = 0,
    start_col = 0,
    end_row = 0,
    end_col = 0,
    optimal_results = {},
  }
end

test("disk.validate accepts a complete result", function()
  assert_eq(disk.validate(base()), nil, "complete data must validate")
end)

test("disk.validate rejects missing end_row", function()
  local data = base()
  data.end_row = nil
  assert_true(disk.validate(data) ~= nil, "missing end_row must be rejected")
end)

test("disk.validate rejects missing end_col", function()
  local data = base()
  data.end_col = nil
  assert_true(disk.validate(data) ~= nil, "missing end_col must be rejected")
end)

test("disk.validate rejects non-numeric end_row", function()
  local data = base()
  data.end_row = "nope"
  assert_true(disk.validate(data) ~= nil, "non-numeric end_row must be rejected")
end)

test("disk.validate accepts diffs/prefix/suffix when present", function()
  local data = base()
  data.prefix = ""
  data.suffix = ""
  data.diffs = {
    { init = { begin_row = 0, begin_col = 0, end_row = 0, end_col = 1 },
      goal = { begin_row = 0, begin_col = 0, end_row = 0, end_col = 2 } },
  }
  assert_eq(disk.validate(data), nil, "well-formed new fields must validate")
end)

test("disk.validate tolerates absent diffs/prefix/suffix (pre-diff saves)", function()
  local data = base()  -- none of the new fields set
  assert_eq(disk.validate(data), nil, "older saves without the new fields must still load")
end)

test("disk.validate rejects wrong-typed new fields", function()
  local data = base()
  data.prefix = 5
  assert_true(disk.validate(data) ~= nil, "non-string prefix must be rejected")

  data = base()
  data.diffs = "nope"
  assert_true(disk.validate(data) ~= nil, "non-table diffs must be rejected")
end)
