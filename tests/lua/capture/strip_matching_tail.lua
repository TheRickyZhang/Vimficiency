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

test("strip_matching_tail: pops exactly the matching suffix", function()
  local seq = { ev("j"), ev("w"), ev(" "), ev("v"), ev("e") }
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 3, "3 entries popped")
  assert_eq(#seq, 2, "2 entries remain")
  assert_eq(seq[1].key_typed_raw, "j", "first survivor")
  assert_eq(seq[2].key_typed_raw, "w", "second survivor")
end)

test("strip_matching_tail: returns 0 on mismatch, leaves seq untouched", function()
  local seq = { ev("j"), ev("w") }
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 0, "no match")
  assert_eq(#seq, 2, "seq unchanged")
end)

test("strip_matching_tail: empty typed_raw is a no-op", function()
  local seq = { ev("j"), ev("w") }
  local popped = key_tracking.strip_matching_tail(seq, "")
  assert_eq(popped, 0, "empty target → no pop")
  assert_eq(#seq, 2, "seq unchanged")
end)

test("strip_matching_tail: empty key_seq is a no-op", function()
  local seq = {}
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 0, "empty seq → no pop")
end)

test("strip_matching_tail: matches even when every entry is a single byte", function()
  -- The path-B scenario from the live debug dump: " ", "v", "e" each a
  -- separate entry, resolution typed = " ve".
  local seq = { ev(" "), ev("v"), ev("e") }
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 3, "all three popped")
  assert_eq(#seq, 0, "seq empty")
end)

test("strip_matching_tail: only pops bytes that are actually the tail", function()
  -- User typed "dj" earlier, then <Space>ve as a mapping. The " ve"
  -- target must only eat " ", "v", "e" — not touch "d", "j".
  local seq = { ev("d"), ev("j"), ev(" "), ev("v"), ev("e") }
  local popped = key_tracking.strip_matching_tail(seq, " ve")
  assert_eq(popped, 3, "3 entries popped")
  assert_eq(seq[1].key_typed_raw, "d", "`d` preserved")
  assert_eq(seq[2].key_typed_raw, "j", "`j` preserved")
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
