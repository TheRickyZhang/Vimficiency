-- tests/lua/test_session_kinds.lua
-- Each creation path in the 2x2 session taxonomy must set the
-- structural (start_kind, end_kind) pair truthfully. These tests pin
-- that contract independent of the rest of session behavior.
--
--   Mark    = (manual, manual)   via session.start
--   Watch   = (manual, auto)     via session.watch
--   Recall  = (auto,   manual)   via session_store.enable_recall
--   Suggest = (auto,   auto)     Recall record flipped at Suggest takeover

local config       = require("vimficiency.config")
local session      = require("vimficiency.session")
local session_store = require("vimficiency.session_store")
local auto_suggest = require("vimficiency.auto_suggest")

local function fresh_buf()
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "line one", "line two" })
  vim.api.nvim_set_current_buf(buf)
  return buf
end

test("new_active_session: rejects invalid start_kind", function()
  local ok, err = pcall(function()
    session_store.new_active_session(
      "id1", -1,
      vim.api.nvim_get_current_win(),
      vim.api.nvim_get_current_buf(),
      { row = 0, col = 0, lines = {} },
      "bogus", "manual"
    )
  end)
  assert_eq(ok, false, "invalid start_kind must error")
  assert_true(tostring(err):find("start_kind", 1, true),
    "error should mention start_kind: " .. tostring(err))
end)

test("new_active_session: rejects invalid end_kind", function()
  local ok, err = pcall(function()
    session_store.new_active_session(
      "id1", -1,
      vim.api.nvim_get_current_win(),
      vim.api.nvim_get_current_buf(),
      { row = 0, col = 0, lines = {} },
      "manual", "bogus"
    )
  end)
  assert_eq(ok, false, "invalid end_kind must error")
  assert_true(tostring(err):find("end_kind", 1, true),
    "error should mention end_kind: " .. tostring(err))
end)

test("session_type_from_kinds: covers all four cells", function()
  assert_eq(session_store.session_type_from_kinds("manual", "manual"), "mark")
  assert_eq(session_store.session_type_from_kinds("manual", "auto"),   "watch")
  assert_eq(session_store.session_type_from_kinds("auto",   "manual"), "recall")
  assert_eq(session_store.session_type_from_kinds("auto",   "auto"),   "suggest")
end)

test("session_type_from_kinds: rejects invalid pairs", function()
  local ok = pcall(session_store.session_type_from_kinds, "bogus", "manual")
  assert_eq(ok, false, "invalid start_kind must error")
  ok = pcall(session_store.session_type_from_kinds, "manual", "bogus")
  assert_eq(ok, false, "invalid end_kind must error")
end)

test("summarize: surfaces type + start_kind + end_kind for a Mark", function()
  local buf = fresh_buf()
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
  fresh_buf()
  local prev_watch = config.watch
  config.watch = { idle = { ms = 60000 }, cooldown_ms = 0 }
  session.watch("kindssumw")
  local rec = session_store.get_active("kindssumw")
  local summary = session_store.summarize(rec.id)
  assert_eq(summary.type,       "watch")
  assert_eq(summary.start_kind, "manual")
  assert_eq(summary.end_kind,   "auto")
  session.close("kindssumw")
  config.watch = prev_watch
end)

test("Mark: session.start yields (manual, manual)", function()
  local buf = fresh_buf()
  session.start("kindsmark")
  local rec = session_store.get_active("kindsmark")
  assert_true(rec ~= nil, "Mark session should be active after start")
  assert_eq(rec.start_kind, "manual")
  assert_eq(rec.end_kind,   "manual")
  session.close("kindsmark")
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("Watch: session.watch yields (manual, auto)", function()
  fresh_buf()
  local prev_watch = config.watch
  config.watch = { idle = { ms = 60000 }, cooldown_ms = 0 }
  session.watch("kindswatch")
  local rec = session_store.get_active("kindswatch")
  assert_true(rec ~= nil, "Watch session should be active after watch")
  assert_eq(rec.start_kind, "manual")
  assert_eq(rec.end_kind,   "auto")
  assert_true(rec.watch_disarm ~= nil, "watch must install a disarm handle")
  session.close("kindswatch")
  config.watch = prev_watch
end)

test("Recall: records created through the queue are (auto, manual)", function()
  -- Directly construct what enable_recall's on_key_event constructs —
  -- the contract is structural, and a headless test can't easily
  -- fire real keystrokes into the global subscriber.
  local rec = session_store.new_active_session(
    "kindsrecall", -1,
    vim.api.nvim_get_current_win(),
    vim.api.nvim_get_current_buf(),
    { row = 0, col = 0, lines = {} },
    "auto", "manual"
  )
  assert_eq(rec.start_kind, "auto")
  assert_eq(rec.end_kind,   "manual")
end)

test("Suggest: fire_idle flips a Recall record to (auto, auto)", function()
  -- Stand in for the real recall-sourced record. The takeover happens
  -- via `active.end_kind = "auto"` inside fire_idle before finish.
  local orig = {
    auto_suggest          = config.auto_suggest,
    is_recall_enabled     = session_store.is_recall_enabled,
    get_active            = session_store.get_active,
    finish_session        = session_store.finish_session,
    summarize             = session_store.summarize,
    compute_result        = session.compute_result_for_active,
  }
  config.auto_suggest = {
    idle = { ms = 200, window = "3s" },
    cooldown_ms = 5000,
  }
  session_store.is_recall_enabled = function() return true end
  local record = { id = "x", start_kind = "auto", end_kind = "manual" }
  session_store.get_active    = function() return record end
  -- Honor the end_kind override to mirror the real store's atomic
  -- contract: the flip happens inside finish_session on success. A
  -- return-true-and-ignore mock would silently skip the assertion here.
  session_store.finish_session = function(_id, _result, _alias, override)
    if override then record.end_kind = override end
    return true
  end
  session_store.summarize      = function()
    return { id = "x", type = "suggest", start_kind = "auto", end_kind = "auto" }
  end
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end

  auto_suggest._for_test.reset()
  auto_suggest._for_test.fire_idle()
  assert_eq(record.start_kind, "auto",
    "start_kind must remain 'auto' — Recall and Suggest share auto start")
  assert_eq(record.end_kind, "auto",
    "end_kind must flip to 'auto' on Suggest takeover")

  config.auto_suggest              = orig.auto_suggest
  session_store.is_recall_enabled  = orig.is_recall_enabled
  session_store.get_active         = orig.get_active
  session_store.finish_session     = orig.finish_session
  session_store.summarize          = orig.summarize
  session.compute_result_for_active = orig.compute_result
end)
