-- tests/lua/key_sequence_builder.lua
-- Pure behavior tests for the native key-sequence reducer used by key_tracking.

local key_tracking = require("vimficiency.capture.key_tracking")

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
    ev("no", "w"),
  })
  assert_eq(seq, "dw")
end)

test("build_sequence: preserves post-change inserted same key", function()
  local seq = key_tracking.build_sequence({
    ev("n", "c"),
    ev("no", "W"),
    ev("i", "W"),
    ev("i", "2"),
  })
  assert_eq(seq, "cWW2")
end)

test("build_sequence: removes operator duplicate before inserted same key", function()
  local seq = key_tracking.build_sequence({
    ev("n", "c"),
    ev("no", "W"),
    ev("no", "W"),
    ev("i", "W"),
    ev("i", "2"),
  })
  assert_eq(seq, "cWW2")
end)

test("build_sequence: removes text-object prefix duplicate", function()
  local seq = key_tracking.build_sequence({
    ev("n", "d"),
    ev("no", "i"),
    ev("n", "i"),
    ev("n", "w"),
  })
  assert_eq(seq, "diw")
end)

test("build_sequence: preserves intentional same-key repetitions", function()
  local seq = key_tracking.build_sequence({
    ev("n", "j"),
    ev("n", "j"),
  })
  assert_eq(seq, "jj")

  seq = key_tracking.build_sequence({
    ev("n", "d"),
    ev("no", "g"),
    ev("no", "g"),
  })
  assert_eq(seq, "dgg")

  seq = key_tracking.build_sequence({
    ev("n", "i"),
    ev("i", "W"),
    ev("i", "W"),
  })
  assert_eq(seq, "iWW")
end)
