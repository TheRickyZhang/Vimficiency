-- tests/lua/test_key_sequence_builder.lua
-- Pure behavior tests for the native key-sequence reducer used by key_tracking.

local key_tracking = require("vimficiency.key_tracking")

local function ev(mode, key_typed)
  return {
    mode = mode,
    key_typed = key_typed,
  }
end

test("build_sequence: empty input returns empty string", function()
  assert_eq(key_tracking.build_sequence({}), "")
end)

test("build_sequence: concatenates non-duplicate keys in order", function()
  local seq = key_tracking.build_sequence({
    ev("n", "d"),
    ev("no", "w"),
    ev("n", "i"),
  })
  assert_eq(seq, "dwi")
end)

test("build_sequence: removes operator-pending re-evaluation duplicate", function()
  local seq = key_tracking.build_sequence({
    ev("n", "d"),
    ev("no", "w"),
    ev("n", "w"),
  })
  assert_eq(seq, "dw")
end)

test("build_sequence: keeps same-key repetition when first event is not operator-pending", function()
  local seq = key_tracking.build_sequence({
    ev("n", "j"),
    ev("n", "j"),
  })
  assert_eq(seq, "jj")
end)

test("build_sequence: keeps same-key repetition when both events are operator-pending", function()
  local seq = key_tracking.build_sequence({
    ev("no", "w"),
    ev("no", "w"),
  })
  assert_eq(seq, "ww")
end)
