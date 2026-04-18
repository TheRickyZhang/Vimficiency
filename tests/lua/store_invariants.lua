-- tests/lua/store_invariants.lua
-- Store-level invariants that don't belong to any one feature:
--   - `end Ns` honestly fails when the queue is younger than N seconds
--     (rather than silently falling back to the oldest record).
--   - session_store.remove rejects alias-shaped input — IDs only, so
--     callers can't accidentally destroy the wrong record via a
--     time-varying recall alias.

local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

test("get_active('Ns'): returns nil when ring is younger than N seconds", function()
  -- Build a recall record whose time_started is ~now; then ask for a
  -- 3-second window. The correct answer is nil — not the record itself —
  -- because the queue doesn't cover 3s yet.
  local _, buf, id = h.seed_recall()

  assert_eq(session_store.get_active("3s"), nil,
    "`3s` against a queue with a single ~0s-old record must not resolve")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("get_active('1s'): resolves when a record is older than the window", function()
  -- Positive counterpart: backdate time_started by 5 seconds so we're
  -- well clear of the 1-second query, independent of hrtime precision.
  local _, buf, id = h.seed_recall({ backdate_s = 5 })

  local resolved = session_store.get_active("1s")
  assert_true(resolved ~= nil, "`1s` must resolve when a 5s-old record exists")
  assert_eq(resolved.id, id)

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("remove: rejects a manual alias-shaped string", function()
  assert_error(function() session_store.remove("a") end,
    "not an alias", "remove must reject alias 'a'")
end)

test("remove: rejects a recall-key alias-shaped string", function()
  assert_error(function() session_store.remove("3") end)
end)

test("remove: rejects a recall-time alias-shaped string", function()
  assert_error(function() session_store.remove("3s") end)
end)

test("remove: accepts a well-formed session id (no-op when unknown)", function()
  -- A real id (contains `__`) that no record holds: should silently no-op,
  -- not error. Guards against false-positive rejection of good input.
  local ok = pcall(session_store.remove, "NoName__20250101-000000-000__1__0001__b1")
  assert_eq(ok, true, "well-formed id must pass the guard")
end)

test("finish failure on a recall record preserves the ring slice", function()
  -- Recall/Suggest records live in the rolling ring and represent a
  -- slice of history. A failed `:Vimfy end Ns` (wrong buffer,
  -- oversized slice, optimizer error) must NOT destroy the record —
  -- a retry over the same window should still resolve. Ring
  -- capacity/age caps own lifetime, not finish failures.
  local _, buf_a, id = h.seed_recall({ backdate_s = 5 })

  -- Drive compute_result_for_active down its "not in original buffer"
  -- path by switching away from buf_a before `finish`.
  local buf_b = h.new_buf({ "y" })

  local ok, err
  h.silence_notify(function() ok, err = pcall(session.finish, "3s") end)
  assert_true(ok, "finish should not propagate errors from a failed compute: " .. tostring(err))

  -- The record must still be in the ring. Query by id via summarize(id)
  -- rather than the time-varying `3s` alias — and in preference to
  -- summarize_all, which can trip over malformed stub records left by
  -- earlier tests (save_default seeds records with no start_kind).
  assert_true(session_store.summarize(id) ~= nil,
    "recall record must survive a finish() failure")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("can_store_manual: finished records do not consume capacity slots", function()
  -- Regression guard for a prior bug where `manual_count` tracked indexed
  -- aliases (active + finished) instead of only actives. After 5 finishes,
  -- new aliases were wrongly blocked even though nothing was active.
  h.new_buf()
  for _, alias in ipairs({ "capa", "capb", "capc", "capd", "cape" }) do
    session.start(alias)
    local rec = session_store.get_active(alias)
    assert_true(rec ~= nil, "start failed for " .. alias)
    -- Finish directly via the store (side-steps compute_result_for_active).
    session_store.finish_session(rec.id, { user_seq = "" }, alias, nil, "manual")
  end
  -- Five finished records hold their alias slots but none are active.
  assert_eq(session_store.can_store_manual("capf"), true,
    "a new alias must be allowed when all existing records are finished")
end)

test("finish failure on a manual record removes it (regression guard)", function()
  -- Symmetric to the recall test: a Mark/Watch session genuinely ends
  -- on finish failure. If we ever "fix" the recall branch by skipping
  -- remove() unconditionally, this test catches it.
  local buf_a = h.new_buf()
  session.start("kfinishfail")
  assert_true(session_store.get_active("kfinishfail") ~= nil,
    "manual session should exist after start")

  local buf_b = h.new_buf({ "y" })
  h.silence_notify(function() pcall(session.finish, "kfinishfail") end)

  assert_eq(session_store.get_active("kfinishfail"), nil,
    "manual record must be removed after finish failure")

  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)
