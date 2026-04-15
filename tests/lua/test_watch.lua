-- tests/lua/test_watch.lua
-- Integration tests for Watch sessions (manual start, auto end on idle).
--
-- Focuses on the pieces this feature introduces: record shape, disarm
-- hook wiring, on_fire re-resolution guard (bail if the alias has been
-- rebound), close/overwrite cleanup. The engine's own timer+cooldown
-- behavior is covered by end_trigger's contract; we don't re-test the
-- chronometer here.

local config       = require("vimficiency.config")
local session      = require("vimficiency.session")
local session_store = require("vimficiency.session_store")
local end_trigger  = require("vimficiency.end_trigger")

local function fresh_buf()
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "hello", "world" })
  vim.api.nvim_set_current_buf(buf)
  return buf
end

local function with_watch_cfg(cfg, fn)
  local prev = config.watch
  config.watch = cfg
  local ok, err = pcall(fn)
  config.watch = prev
  if not ok then error(err, 2) end
end

test("watch: refuses to start when config.watch is falsy", function()
  local prev = config.watch
  config.watch = false
  -- Swallow the user-facing notify; we care about the store state.
  local orig_notify = vim.notify
  vim.notify = function() end
  session.watch("wnone")
  vim.notify = orig_notify
  assert_eq(session_store.get_active("wnone"), nil,
    "no record should be created when watch is disabled")
  config.watch = prev
end)

test("watch: creates (manual, auto) record with disarm handle", function()
  fresh_buf()
  with_watch_cfg({ idle = { ms = 60000 }, cooldown_ms = 0 }, function()
    session.watch("wrec")
    local rec = session_store.get_active("wrec")
    assert_true(rec ~= nil, "watch record should be active")
    assert_eq(rec.start_kind, "manual")
    assert_eq(rec.end_kind,   "auto")
    assert_true(type(rec.watch_disarm) == "function",
      "watch_disarm must be installed on the record")
    session.close("wrec")
  end)
end)

test("watch: close disarms the idle trigger", function()
  fresh_buf()
  with_watch_cfg({ idle = { ms = 60000 }, cooldown_ms = 0 }, function()
    session.watch("wclose")
    local rec = session_store.get_active("wclose")
    assert_true(rec ~= nil)
    -- Prove the subscriber is live before close.
    local before = require("vimficiency.key_tracking")
      .is_global_attached("watch_" .. rec.id)
    assert_eq(before, true, "global subscriber must be live while watching")

    session.close("wclose")
    local after = require("vimficiency.key_tracking")
      .is_global_attached("watch_" .. rec.id)
    assert_eq(after, false, "close must detach the global subscriber")
  end)
end)

test("watch: overwrite disarms the prior trigger", function()
  fresh_buf()
  with_watch_cfg({ idle = { ms = 60000 }, cooldown_ms = 0 }, function()
    session.watch("wover")
    local rec_a = session_store.get_active("wover")
    assert_true(rec_a ~= nil)
    local name_a = "watch_" .. rec_a.id

    session.watch("wover")  -- overwrite
    local rec_b = session_store.get_active("wover")
    assert_true(rec_b ~= nil)
    assert_true(rec_a.id ~= rec_b.id,
      "overwrite must allocate a new id (not reuse the old one)")

    local kt = require("vimficiency.key_tracking")
    assert_eq(kt.is_global_attached(name_a), false,
      "previous watch's global subscriber must be detached on overwrite")
    assert_eq(kt.is_global_attached("watch_" .. rec_b.id), true,
      "new watch's global subscriber must be live")
    session.close("wover")
  end)
end)

test("watch: finish (manual :Vimfy end) disarms the trigger", function()
  -- End-to-end finish needs the C++ optimizer; side-step by calling the
  -- store's finish_session directly. This still exercises the disarm
  -- hook in finish_session, which is what matters here.
  fresh_buf()
  with_watch_cfg({ idle = { ms = 60000 }, cooldown_ms = 0 }, function()
    session.watch("wfinish")
    local rec = session_store.get_active("wfinish")
    assert_true(rec ~= nil)
    local name = "watch_" .. rec.id

    local fake_result = { user_seq = "" }
    assert_eq(session_store.finish_session(rec.id, fake_result, "wfinish", nil, "watch_idle"), true)

    local kt = require("vimficiency.key_tracking")
    assert_eq(kt.is_global_attached(name), false,
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
  disarm_1()
end)
