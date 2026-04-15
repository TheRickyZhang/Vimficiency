-- tests/lua/test_auto_suggest.lua
-- Unit tests for fire_idle's dedup/failure-throttle behavior. The actual
-- cooldown timer is owned by end_trigger.arm_idle; fire_idle communicates
-- with it by returning a boolean (true = "this fire counted, refresh
-- cooldown"; false = "no-op, don't lock out the next tick").
-- Monkey-patches session_store and session with stubs so we can drive
-- outcomes without a live queue or the C++ optimizer.

local config        = require("vimficiency.config")
local auto_suggest  = require("vimficiency.auto_suggest")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session_store")

local fire_idle          = auto_suggest._for_test.fire_idle
local fire_keys          = auto_suggest._for_test.fire_keys
local fire_cost          = auto_suggest._for_test.fire_cost
local get_last_fp        = auto_suggest._for_test.get_last_fingerprint
local reset              = auto_suggest._for_test.reset

-- Save originals so each test starts from a clean slate.
local orig = {
  config_auto_suggest          = config.auto_suggest,
  get_active                   = session_store.get_active,
  finish_session               = session_store.finish_session,
  summarize                    = session_store.summarize,
  compute_result_for_active    = session.compute_result_for_active,
}

local function restore()
  config.auto_suggest              = orig.config_auto_suggest
  session_store.get_active         = orig.get_active
  session_store.finish_session     = orig.finish_session
  session_store.summarize          = orig.summarize
  session.compute_result_for_active = orig.compute_result_for_active
end

local function setup_enabled()
  reset()
  config.auto_suggest = {
    idle = { ms = 200, window = "3s" },
    cooldown_ms = 5000,
  }
  session_store.get_active = function()
    return { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  end
  session_store.finish_session = function() return true end
  session_store.summarize = function()
    return {
      id = "fake-id",
      type = "suggest", start_kind = "auto", end_kind = "auto",
      result = {
        start_row = 0, start_col = 0, end_row = 0, end_col = 0,
        user_seq = "x", user_cost = 0.0,
        optimal_results = {},
      },
    }
  end
end

test("fire_idle: no-op when window unresolved (queue too young)", function()
  setup_enabled()
  session_store.get_active = function() return nil end
  local counted = fire_idle()
  assert_eq(counted, false, "unresolved window should not count")
  restore()
end)

test("fire_idle: compute failure still counts (throttle)", function()
  setup_enabled()
  session.compute_result_for_active = function() return nil, "rejected" end
  local counted = fire_idle()
  assert_eq(counted, true,
    "compute failure must count as a fire so retries respect cooldown")
  restore()
end)

test("fire_idle: finish failure also counts", function()
  setup_enabled()
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  session_store.finish_session = function() return false end
  local counted = fire_idle()
  assert_eq(counted, true,
    "store failure must count as a fire so retries respect cooldown")
  restore()
end)

test("fire_idle: success counts and records fingerprint", function()
  setup_enabled()
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  local counted = fire_idle()
  assert_eq(counted, true, "success should count")
  assert_true(get_last_fp() ~= nil, "success should record a fingerprint")
  restore()
end)

test("fire_idle: promotes end_kind from manual to auto on takeover", function()
  setup_enabled()
  local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  session_store.get_active = function() return captured end
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  -- Mirror finish_session's real contract: the end_kind override is
  -- applied atomically with the status transition. A failing finish
  -- leaves end_kind untouched.
  session_store.finish_session = function(_id, _result, _alias, override)
    if override then captured.end_kind = override end
    return true
  end
  fire_idle()
  assert_eq(captured.end_kind, "auto",
    "Recall -> Suggest promotion must flip end_kind at finish time")
  assert_eq(captured.start_kind, "auto",
    "start_kind must stay 'auto' (Suggest is still auto-start)")
  restore()
end)

test("fire_idle: compute failure leaves end_kind = 'manual' (no speculative flip)", function()
  setup_enabled()
  local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  session_store.get_active = function() return captured end
  session.compute_result_for_active = function() return nil, "rejected" end
  -- Atomic contract: finish is never reached, so end_kind must not
  -- mutate. A regression that speculatively flipped end_kind before
  -- compute would leave a Recall record mislabeled as Suggest here.
  fire_idle()
  assert_eq(captured.end_kind, "manual",
    "compute failure must not mutate end_kind")
  restore()
end)

test("fire_idle: finish failure leaves end_kind = 'manual' (atomic override)", function()
  setup_enabled()
  local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  session_store.get_active = function() return captured end
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  -- Failing finish_session must NOT apply the override. This is the
  -- atomicity contract: end_kind mutation and status transition share
  -- the same guard in finish_session.
  session_store.finish_session = function() return false end
  fire_idle()
  assert_eq(captured.end_kind, "manual",
    "finish failure must not mutate end_kind — atomic with status transition")
  restore()
end)

--------------------------------------------------------------------------------
-- fire_keys: derives window from cfg.keys.every
--------------------------------------------------------------------------------

test("fire_keys: passes tostring(every) as the recall window", function()
  setup_enabled()
  config.auto_suggest = { keys = { every = 30 }, cooldown_ms = 5000 }
  local captured_alias = nil
  session_store.get_active = function(alias)
    captured_alias = alias
    return { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  end
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  fire_keys()
  assert_eq(captured_alias, "30",
    "fire_keys resolves its window from cfg.keys.every (no separate knob)")
  restore()
end)

test("fire_keys: no-op when cfg.keys is absent", function()
  setup_enabled()
  config.auto_suggest = { idle = { ms = 200, window = "3s" }, cooldown_ms = 5000 }
  local counted = fire_keys()
  assert_eq(counted, false, "unconfigured trigger must not count")
  restore()
end)

test("fire_keys: success path records fingerprint (shared state with fire_idle)", function()
  setup_enabled()
  config.auto_suggest = { keys = { every = 50 }, cooldown_ms = 5000 }
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  local counted = fire_keys()
  assert_eq(counted, true)
  assert_true(get_last_fp() ~= nil,
    "keys trigger must write to the same last_fingerprint as idle")
  restore()
end)

--------------------------------------------------------------------------------
-- fire_cost: gate on user_cost vs m*optimal+b
--------------------------------------------------------------------------------

test("fire_cost: gate failure counts but does not fingerprint (user was efficient)", function()
  setup_enabled()
  config.auto_suggest = {
    cost = { m = 2.0, b = 0.0, ms = 300, window = "100" },
    cooldown_ms = 5000,
  }
  -- 5 > 2*10+0 == 20 → false → gate fails → suppress notify, still count.
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", user_cost = 5,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }
  end
  local counted = fire_cost()
  assert_eq(counted, true, "gate failure must still count (optimizer ran)")
  assert_eq(get_last_fp(), nil,
    "gate failure must not record fingerprint — nothing was surfaced")
  restore()
end)

test("fire_cost: gate pass records fingerprint", function()
  setup_enabled()
  config.auto_suggest = {
    cost = { m = 1.0, b = 0.0, ms = 300, window = "100" },
    cooldown_ms = 5000,
  }
  -- 20 > 1*10+0 == 10 → true → gate passes.
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", user_cost = 20,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }
  end
  local counted = fire_cost()
  assert_eq(counted, true)
  assert_true(get_last_fp() ~= nil,
    "gate pass must fingerprint (the shown suggestion)")
  restore()
end)

test("fire_cost: empty optimal_results suppresses notify", function()
  setup_enabled()
  config.auto_suggest = {
    cost = { m = 1.0, b = 0.0, ms = 300, window = "100" },
    cooldown_ms = 5000,
  }
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", user_cost = 20,
      optimal_results = {},
    }
  end
  local counted = fire_cost()
  assert_eq(counted, true, "optimizer ran, still count")
  assert_eq(get_last_fp(), nil,
    "no optimal result to compare against → suppress")
  restore()
end)

--------------------------------------------------------------------------------
-- Reason plumbing: each trigger must pass its literal reason through to
-- finish_session so the header can print it.
--------------------------------------------------------------------------------

test("fire_idle: passes 'suggest_idle' as the reason", function()
  setup_enabled()
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  local captured_reason = nil
  session_store.finish_session = function(_id, _result, _alias, _override, reason)
    captured_reason = reason
    return true
  end
  fire_idle()
  assert_eq(captured_reason, "suggest_idle")
  restore()
end)

test("fire_keys: passes 'suggest_keys' as the reason", function()
  setup_enabled()
  config.auto_suggest = { keys = { every = 30 }, cooldown_ms = 5000 }
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  local captured_reason = nil
  session_store.finish_session = function(_id, _result, _alias, _override, reason)
    captured_reason = reason
    return true
  end
  fire_keys()
  assert_eq(captured_reason, "suggest_keys")
  restore()
end)

test("fire_cost: passes 'suggest_cost' as the reason", function()
  setup_enabled()
  config.auto_suggest = {
    cost = { m = 1.0, b = 0.0, ms = 300, window = "100" },
    cooldown_ms = 5000,
  }
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", user_cost = 20,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }
  end
  local captured_reason = nil
  session_store.finish_session = function(_id, _result, _alias, _override, reason)
    captured_reason = reason
    return true
  end
  fire_cost()
  assert_eq(captured_reason, "suggest_cost")
  restore()
end)

test("fire_cost: gate fail finishes the record (consume, not leak)", function()
  setup_enabled()
  local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  session_store.get_active = function() return captured end
  config.auto_suggest = {
    cost = { m = 2.0, b = 0.0, ms = 300, window = "100" },
    cooldown_ms = 5000,
  }
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", user_cost = 5,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }
  end
  local finish_called = false
  session_store.finish_session = function() finish_called = true; return true end
  fire_cost()
  assert_eq(finish_called, true,
    "gate failure must still finish the record so next tick doesn't re-analyze it")
  restore()
end)
