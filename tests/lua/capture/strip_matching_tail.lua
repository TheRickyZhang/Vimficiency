-- tests/lua/capture/strip_matching_tail.lua
-- Pure-function tests for `key_tracking.strip_matching_tail`. The same
-- helper is used by the per-session on_key handler (inline) and by
-- `session_store.strip_recall_pre_resolution` (via fan-out) — testing it
-- directly here means a regression shows up against the shared helper
-- rather than split across both call sites.

local key_tracking = require("vimficiency.capture.key_tracking")

---@param raw string
---@return table
local function ev(raw)
  return {
    mode = "n",
    key_sent_raw = raw, key_sent = raw,
    key_typed_raw = raw, key_typed = raw,
  }
end

test("strip_matching_tail: pops exactly the matching suffix and preserves prefix", function()
  local seq = { ev("j"), ev("w"), ev(" "), ev("v"), ev("e") }
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 3, "3 entries popped")
  assert_eq(#seq, 2, "2 entries remain")
  assert_eq(seq[1].key_typed_raw, "j", "first survivor")
  assert_eq(seq[2].key_typed_raw, "w", "second survivor")

  -- The path-B scenario from the live debug dump: " ", "v", "e" each a
  -- separate entry, resolution typed = " ve".
  seq = { ev(" "), ev("v"), ev("e") }
  popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 3, "all three popped")
  assert_eq(#seq, 0, "seq empty")
end)

test("strip_matching_tail: no-op cases leave sequence intact", function()
  for _, case in ipairs({
    { seq = { ev("j"), ev("w") }, target = " ve", msg = "mismatch" },
    { seq = { ev("j"), ev("w") }, target = "", msg = "empty target" },
    { seq = {}, target = " ve", msg = "empty seq" },
  }) do
    local original_len = #case.seq
    local popped = key_tracking.strip_matching_tail(case.seq, case.target)
    assert_eq(popped, 0, case.msg)
    assert_eq(#case.seq, original_len, case.msg .. " should not mutate seq")
  end
end)

test("strip_matching_tail: handles multi-byte key_typed_raw at the boundary", function()
  -- If a prior entry has a multi-byte `key_typed_raw` whose bytes
  -- overlap with the target prefix, we still match correctly via
  -- byte-concatenation.
  local seq = { ev("x"), ev(" v"), ev("e") }
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 2, "two entries popped (the multi-byte and the `e`)")
  assert_eq(#seq, 1, "only the `x` survives")
end)
