-- tests/lua/test_store_invariants.lua
-- Store-level invariants that don't belong to any one feature:
--   - `end Ns` honestly fails when the queue is younger than N seconds
--     (rather than silently falling back to the oldest record).
--   - session_store.remove rejects alias-shaped input — IDs only, so
--     callers can't accidentally destroy the wrong record via a
--     time-varying recall alias.

local session_store = require("vimficiency.session_store")
local util          = require("vimficiency.util")

local function fresh_buf()
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "x" })
  vim.api.nvim_set_current_buf(buf)
  return buf
end

test("get_active('Ns'): returns nil when ring is younger than N seconds", function()
  -- Build a recall record whose time_started is ~now; then ask for a
  -- 3-second window. The correct answer is nil — not the record itself —
  -- because the queue doesn't cover 3s yet.
  local buf = fresh_buf()
  local win = vim.api.nvim_get_current_win()
  local id = util.new_id(buf)
  local rec = session_store.new_active_session(
    id, -1, win, buf,
    { row = 0, col = 0, lines = { "x" } },
    "auto", "manual"
  )
  rec.first_mode = "n"
  session_store.store_recall(rec)

  local resolved = session_store.get_active("3s")
  assert_eq(resolved, nil,
    "`3s` against a queue with a single ~0s-old record must not resolve")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("get_active('1s'): resolves when a record is older than the window", function()
  -- Positive counterpart to the "younger than N" test: if the ring
  -- contains a record that IS older than the window, resolution must
  -- succeed. Backdate time_started by 5 seconds so we're well clear of
  -- the 1-second query, independent of hrtime precision.
  local buf = fresh_buf()
  local win = vim.api.nvim_get_current_win()
  local id = util.new_id(buf)
  local rec = session_store.new_active_session(
    id, -1, win, buf,
    { row = 0, col = 0, lines = { "x" } },
    "auto", "manual"
  )
  rec.first_mode = "n"
  rec.time_started = vim.uv.hrtime() - 5 * 1e9
  session_store.store_recall(rec)

  local resolved = session_store.get_active("1s")
  assert_true(resolved ~= nil, "`1s` must resolve when a 5s-old record exists")
  assert_eq(resolved.id, id)

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("remove: rejects a manual alias-shaped string", function()
  local ok, err = pcall(session_store.remove, "a")
  assert_eq(ok, false, "remove must reject alias 'a'")
  assert_true(tostring(err):find("not an alias", 1, true),
    "error should explain the mistake: " .. tostring(err))
end)

test("remove: rejects a recall-key alias-shaped string", function()
  local ok = pcall(session_store.remove, "3")
  assert_eq(ok, false, "remove must reject recall-key alias '3'")
end)

test("remove: rejects a recall-time alias-shaped string", function()
  local ok = pcall(session_store.remove, "3s")
  assert_eq(ok, false, "remove must reject recall-time alias '3s'")
end)

test("remove: accepts a well-formed session id (no-op when unknown)", function()
  -- A real id (contains `__`) that no record holds: should silently no-op,
  -- not error. Guards against false-positive rejection of good input.
  local ok = pcall(session_store.remove, "NoName__20250101-000000-000__1__0001__b1")
  assert_eq(ok, true, "well-formed id must pass the guard")
end)
