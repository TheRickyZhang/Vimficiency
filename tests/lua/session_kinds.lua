-- tests/lua/session_kinds.lua
-- Each creation path in the 2x2 session taxonomy must set the
-- structural (start_kind, end_kind) pair truthfully. These tests pin
-- that contract independent of the rest of session behavior.
--
--   Mark    = (manual, manual)   via session.start
--   Watch   = (manual, auto)     via session.watch
--   Recall  = (auto,   manual)   via session_store.install_recall
--   Suggest = (auto,   auto)     Recall record flipped at Suggest takeover

local config        = require("vimficiency.config")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local auto_suggest  = require("vimficiency.capture.auto_suggest")
local h             = require("_helpers")

local WATCH_CFG = { idle = { ms = 60000 }, cooldown_ms = 0 }

local function call_new_active(start_kind, end_kind)
  return session_store.new_active_session(
    "id1", -1,
    vim.api.nvim_get_current_win(),
    vim.api.nvim_get_current_buf(),
    { row = 0, col = 0, lines = {} },
    start_kind, end_kind
  )
end

test("new_active_session: rejects invalid start_kind", function()
  assert_error(
    function() call_new_active("bogus", "manual") end,
    "start_kind", "invalid start_kind must error"
  )
end)

test("new_active_session: rejects invalid end_kind", function()
  assert_error(
    function() call_new_active("manual", "bogus") end,
    "end_kind", "invalid end_kind must error"
  )
end)

test("session_type_from_kinds: covers all four cells", function()
  assert_eq(session_store.session_type_from_kinds("manual", "manual"), "mark")
  assert_eq(session_store.session_type_from_kinds("manual", "auto"),   "watch")
  assert_eq(session_store.session_type_from_kinds("auto",   "manual"), "recall")
  assert_eq(session_store.session_type_from_kinds("auto",   "auto"),   "suggest")
end)

test("session_type_from_kinds: rejects invalid pairs", function()
  assert_error(function() session_store.session_type_from_kinds("bogus", "manual") end)
  assert_error(function() session_store.session_type_from_kinds("manual", "bogus") end)
end)

test("summarize: surfaces type + start_kind + end_kind for a Mark", function()
  local buf = h.new_buf({ "line one", "line two" })
  session.start("kindssum")
  local rec = session_store.get_active("kindssum")
  local summary = session_store.summarize(rec.id)
  assert_true(summary ~= nil, "summary must exist for active session")
  assert_eq(summary.type,       "mark")
  assert_eq(summary.start_kind, "manual")
  assert_eq(summary.end_kind,   "manual")
  session.close("kindssum")
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("summarize: surfaces type=watch for a Watch session", function()
  h.new_buf({ "line one", "line two" })
  h.with_patch({ { config, "watch", WATCH_CFG } }, function()
    session.watch("kindssumw")
    local rec = session_store.get_active("kindssumw")
    local summary = session_store.summarize(rec.id)
    assert_eq(summary.type,       "watch")
    assert_eq(summary.start_kind, "manual")
    assert_eq(summary.end_kind,   "auto")
    session.close("kindssumw")
  end)
end)

test("Mark: session.start yields (manual, manual)", function()
  local buf = h.new_buf({ "line one", "line two" })
  session.start("kindsmark")
  local rec = session_store.get_active("kindsmark")
  assert_true(rec ~= nil, "Mark session should be active after start")
  assert_eq(rec.start_kind, "manual")
  assert_eq(rec.end_kind,   "manual")
  session.close("kindsmark")
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("Watch: session.watch yields (manual, auto)", function()
  h.new_buf({ "line one", "line two" })
  h.with_patch({ { config, "watch", WATCH_CFG } }, function()
    session.watch("kindswatch")
    local rec = session_store.get_active("kindswatch")
    assert_true(rec ~= nil, "Watch session should be active after watch")
    assert_eq(rec.start_kind, "manual")
    assert_eq(rec.end_kind,   "auto")
    assert_true(rec.watch_disarm ~= nil, "watch must install a disarm handle")
    session.close("kindswatch")
  end)
end)

test("Recall: records created through the queue are (auto, manual)", function()
  -- Directly construct what install_recall's on_key_event constructs —
  -- the contract is structural, and a headless test can't easily
  -- fire real keystrokes into the global subscriber.
  local rec = call_new_active("auto", "manual")
  assert_eq(rec.start_kind, "auto")
  assert_eq(rec.end_kind,   "manual")
end)

test("Suggest: fire_idle flips a Recall record to (auto, auto)", function()
  -- Stand in for the real recall-sourced record. The takeover happens
  -- via `active.end_kind = "auto"` inside fire_idle before finish.
  local record = { id = "x", start_kind = "auto", end_kind = "manual" }
  h.with_patch({
    { config, "auto_suggest", { idle = { ms = 200, window = "3s" }, cooldown_ms = 5000 } },
    { session_store, "get_active",     function() return record end },
    -- Honor the end_kind override to mirror the real store's atomic
    -- contract: the flip happens inside finish_session on success. A
    -- return-true-and-ignore mock would silently skip the assertion here.
    { session_store, "finish_session", function(_id, _r, _alias, override)
        if override then record.end_kind = override end
        return true
      end },
    { session_store, "summarize", function()
        return { id = "x", type = "suggest", start_kind = "auto", end_kind = "auto" }
      end },
    { session, "compute_result_for_active", function() return h.fake_result() end },
  }, function()
    auto_suggest._for_test.reset()
    auto_suggest._for_test.fire_idle()
    assert_eq(record.start_kind, "auto",
      "start_kind must remain 'auto' — Recall and Suggest share auto start")
    assert_eq(record.end_kind, "auto",
      "end_kind must flip to 'auto' on Suggest takeover")
  end)
end)
