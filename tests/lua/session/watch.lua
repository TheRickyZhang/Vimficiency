-- tests/lua/watch.lua
-- Integration tests for Watch sessions (manual start, auto end on idle).
--
-- Focuses on the pieces this feature introduces: record shape, disarm
-- hook wiring, on_fire re-resolution guard (bail if the alias has been
-- rebound), close/overwrite cleanup. The engine's own timer+cooldown
-- behavior is covered by end_trigger's contract; we don't re-test the
-- chronometer here.

local config        = require("vimficiency.config")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local end_trigger   = require("vimficiency.capture.end_trigger")
local key_tracking  = require("vimficiency.capture.key_tracking")
local h             = require("_helpers")

local WATCH_CFG = { idle = { ms = 60000 }, cooldown_ms = 0 }

local function with_watch_cfg(cfg, fn)
  h.with_patch({ { config, "watch", cfg } }, fn)
end

test("watch: refuses to start when config.watch is falsy", function()
  with_watch_cfg(false, function()
    h.silence_notify(function() session.watch("wnone") end)
    assert_eq(session_store.get_active("wnone"), nil,
      "no record should be created when watch is disabled")
  end)
end)

test("watch: creates (manual, auto) record with disarm handle", function()
  h.new_buf({ "hello", "world" })
  with_watch_cfg(WATCH_CFG, function()
    session.watch("wrec")
    local rec = session_store.get_active("wrec")
    assert_true(rec ~= nil, "watch record should be active")
    rec = assert(rec)
    assert_eq(rec.start_kind, "manual")
    assert_eq(rec.end_kind,   "auto")
    assert_true(type(rec.watch_disarm) == "function",
      "watch_disarm must be installed on the record")
    session.close("wrec")
  end)
end)

test("watch: close disarms the idle trigger", function()
  h.new_buf({ "hello", "world" })
  with_watch_cfg(WATCH_CFG, function()
    session.watch("wclose")
    local rec = session_store.get_active("wclose")
    assert_true(rec ~= nil)
    rec = assert(rec)
    assert_eq(key_tracking.is_global_attached("watch_" .. rec.id), true,
      "global subscriber must be live while watching")

    session.close("wclose")
    assert_eq(key_tracking.is_global_attached("watch_" .. rec.id), false,
      "close must detach the global subscriber")
  end)
end)

test("watch: overwrite disarms the prior trigger", function()
  h.new_buf({ "hello", "world" })
  with_watch_cfg(WATCH_CFG, function()
    session.watch("wover")
    local rec_a = session_store.get_active("wover")
    assert_true(rec_a ~= nil)
    rec_a = assert(rec_a)
    local name_a = "watch_" .. rec_a.id

    session.watch("wover")  -- overwrite
    local rec_b = session_store.get_active("wover")
    assert_true(rec_b ~= nil)
    rec_b = assert(rec_b)
    assert_true(rec_a.id ~= rec_b.id,
      "overwrite must allocate a new id")

    assert_eq(key_tracking.is_global_attached(name_a), false,
      "previous watch's global subscriber must be detached on overwrite")
    assert_eq(key_tracking.is_global_attached("watch_" .. rec_b.id), true,
      "new watch's global subscriber must be live")
    session.close("wover")
  end)
end)

test("watch: finish (manual :Vimfy finish) disarms the trigger", function()
  -- Call the store directly so the test stays Lua-only while still exercising
  -- the finish-session disarm hook.
  h.new_buf({ "hello", "world" })
  with_watch_cfg(WATCH_CFG, function()
    session.watch("wfinish")
    local rec = session_store.get_active("wfinish")
    assert_true(rec ~= nil)
    rec = assert(rec)
    local name = "watch_" .. rec.id

    local fake_result = h.fake_result({ user_seq = "" })
    assert_eq(
      session_store.finish_session(rec.id, fake_result, "wfinish", nil, "watch_idle"),
      true)

    assert_eq(key_tracking.is_global_attached(name), false,
      "finish_session must disarm the watch trigger")
    assert_eq(fake_result.finish_reason, "watch_idle",
      "finish_session must stamp the reason onto the result")
  end)
end)

test("end_trigger.arm_idle: distinct names coexist", function()
  -- Two simultaneous arms under different names must both attach
  -- cleanly. Guards the Watch/Suggest independence invariant.
  local disarm_a = end_trigger.arm_idle({
    name = "endt_test_a",
    idle_ms = 60000, cooldown_ms = 0,
    on_fire = function() return true end,
  })
  local disarm_b = end_trigger.arm_idle({
    name = "endt_test_b",
    idle_ms = 60000, cooldown_ms = 0,
    on_fire = function() return true end,
  })
  assert_true(disarm_a ~= nil, "first arm should succeed")
  assert_true(disarm_b ~= nil, "second arm under different name should succeed")
  disarm_a = assert(disarm_a)
  disarm_b = assert(disarm_b)
  disarm_a()
  disarm_b()
end)

test("end_trigger.arm_idle: name collision returns nil", function()
  local disarm_1 = end_trigger.arm_idle({
    name = "endt_test_collision",
    idle_ms = 60000, cooldown_ms = 0,
    on_fire = function() return true end,
  })
  local disarm_2 = end_trigger.arm_idle({
    name = "endt_test_collision",
    idle_ms = 60000, cooldown_ms = 0,
    on_fire = function() return true end,
  })
  assert_true(disarm_1 ~= nil)
  assert_eq(disarm_2, nil, "duplicate name must refuse to arm")
  disarm_1 = assert(disarm_1)
  disarm_1()
end)
