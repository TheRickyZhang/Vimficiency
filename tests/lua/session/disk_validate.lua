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
