-- tests/lua/manual_autodrop.lua
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

test("manual_should_evict: active nearby sessions are retained", function()
  local s = fake_session(0, nil)
  assert_eq(session.manual_should_evict(s, 0, 1e15), nil)
  local now = 1e15
  local recent = fake_session(100, now - 10 * NS_PER_SEC)
  assert_eq(session.manual_should_evict(recent, 105, now), nil)
end)

test("manual_should_evict: last key older than timeout → idle reason", function()
  local now = 1e15
  local stale = now - (config.MANUAL_IDLE_TIMEOUT_SECONDS + 1) * NS_PER_SEC
  local s = fake_session(50, stale)
  local reason = session.manual_should_evict(s, 50, now)
  assert_true(reason ~= nil, "expected eviction")
  reason = assert(reason)
  assert_true(reason:find("idle", 1, true) ~= nil,
    "expected idle reason, got: " .. tostring(reason))
end)

test("manual_should_evict: cursor drift beyond search window evicts", function()
  local now = 1e15
  local below = fake_session(0, now - 1 * NS_PER_SEC)
  local reason = session.manual_should_evict(below, config.MAX_SEARCH_LINES + 1, now)
  assert_true(reason ~= nil, "expected eviction")
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason, got: " .. tostring(reason))

  -- Start at row 1000, cursor at row 499: upward drift exceeds the window.
  local above = fake_session(1000, now - 1 * NS_PER_SEC)
  reason = session.manual_should_evict(above, 499, now)
  assert_true(reason ~= nil, "expected eviction (upward drift)")
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason, got: " .. tostring(reason))

  local boundary = fake_session(0, now - 1 * NS_PER_SEC)
  assert_eq(session.manual_should_evict(boundary, config.MAX_SEARCH_LINES - 1, now), nil)
end)

test("manual_should_evict: drift wins over idle and nil key_seq is safe", function()
  local now = 1e15
  local stale = now - (config.MANUAL_IDLE_TIMEOUT_SECONDS + 10) * NS_PER_SEC
  local s = fake_session(0, stale)
  local reason = session.manual_should_evict(
    s, config.MAX_SEARCH_LINES + 5, now
  )
  assert_true(reason ~= nil, "expected eviction")
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason (drift checked before idle), got: " .. tostring(reason))

  local finished_shape = fake_session(0, nil, true)
  assert_eq(session.manual_should_evict(finished_shape, 0, now), nil)
  reason = session.manual_should_evict(
    finished_shape, config.MAX_SEARCH_LINES + 1, now
  )
  assert_true(reason ~= nil)
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil)
end)
