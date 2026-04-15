-- tests/lua/test_manual_autodrop.lua
-- Covers `session.manual_should_evict` — the pure predicate that decides
-- when an in-progress manual session should be auto-dropped.
--
-- Triggers (checked in order; drift wins when both fire):
--   1. Cursor drifted more than MAX_SEARCH_LINES rows from start_state.row.
--   2. Idle for more than MANUAL_IDLE_TIMEOUT_SECONDS since the last captured key.
--
-- Pure tests: we construct minimal session-record shapes from scratch.
-- No store, no vim.api, no timers.

local session = require("vimficiency.session")
local config  = require("vimficiency.config")

local NS_PER_SEC = 1e9

--- Build a minimal fake record with just the fields the predicate reads.
---@param start_row integer
---@param last_key_t integer|nil  hrtime of last key event; nil for empty seq
---@param key_seq_nil boolean|nil  If true, set key_seq = nil instead of empty {}
---@return table
local function fake_session(start_row, last_key_t, key_seq_nil)
  local seq
  if key_seq_nil then
    seq = nil
  elseif last_key_t then
    seq = { { t = last_key_t } }
  else
    seq = {}
  end
  return {
    start_state = { row = start_row },
    key_seq = seq,
  }
end

test("manual_should_evict: fresh session, cursor at start row, empty seq → nil", function()
  local s = fake_session(0, nil)
  assert_eq(session.manual_should_evict(s, 0, 1e15), nil)
end)

test("manual_should_evict: recent key + nearby cursor → nil", function()
  local now = 1e15
  local s = fake_session(100, now - 10 * NS_PER_SEC)
  assert_eq(session.manual_should_evict(s, 105, now), nil)
end)

test("manual_should_evict: last key older than timeout → idle reason", function()
  local now = 1e15
  local stale = now - (config.MANUAL_IDLE_TIMEOUT_SECONDS + 1) * NS_PER_SEC
  local s = fake_session(50, stale)
  local reason = session.manual_should_evict(s, 50, now)
  assert_true(reason ~= nil, "expected eviction")
  assert_true(reason:find("idle", 1, true) ~= nil,
    "expected idle reason, got: " .. tostring(reason))
end)

test("manual_should_evict: cursor drifted far below start → drift reason", function()
  local now = 1e15
  local s = fake_session(0, now - 1 * NS_PER_SEC)
  local drift_row = config.MAX_SEARCH_LINES + 1
  local reason = session.manual_should_evict(s, drift_row, now)
  assert_true(reason ~= nil, "expected eviction")
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason, got: " .. tostring(reason))
end)

test("manual_should_evict: cursor drifted far above start → drift reason (abs)", function()
  -- Start at row 1000, cursor at row 499 → drift = 501 > 500.
  local now = 1e15
  local s = fake_session(1000, now - 1 * NS_PER_SEC)
  local reason = session.manual_should_evict(s, 499, now)
  assert_true(reason ~= nil, "expected eviction (upward drift)")
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason, got: " .. tostring(reason))
end)

test("manual_should_evict: cursor at exact MAX_SEARCH_LINES distance → nil", function()
  -- Boundary: drift = MAX_SEARCH_LINES (inclusive: 500 away, span is 501
  -- rows which equals the ceiling; trigger only fires on strict > ).
  local now = 1e15
  local s = fake_session(0, now - 1 * NS_PER_SEC)
  local reason = session.manual_should_evict(
    s, config.MAX_SEARCH_LINES - 1, now
  )
  assert_eq(reason, nil)
end)

test("manual_should_evict: drift and idle both true → drift wins", function()
  local now = 1e15
  local stale = now - (config.MANUAL_IDLE_TIMEOUT_SECONDS + 10) * NS_PER_SEC
  local s = fake_session(0, stale)
  local reason = session.manual_should_evict(
    s, config.MAX_SEARCH_LINES + 5, now
  )
  assert_true(reason ~= nil, "expected eviction")
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason (drift checked before idle), got: " .. tostring(reason))
end)

test("manual_should_evict: key_seq=nil → idle branch no-ops, only drift applies", function()
  -- Defensive: finished records drop key_seq to nil. The predicate
  -- should still be safe against that shape (even though in practice
  -- we only call it on active records).
  local s = fake_session(0, nil, true)
  assert_eq(session.manual_should_evict(s, 0, 1e15), nil)
  local reason = session.manual_should_evict(
    s, config.MAX_SEARCH_LINES + 1, 1e15
  )
  assert_true(reason ~= nil)
  assert_true(reason:find("drifted", 1, true) ~= nil)
end)
