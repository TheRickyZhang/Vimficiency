-- tests/lua/test_auto_suggest.lua
-- Unit tests for fire_idle's dedup/cooldown/failure-throttle behavior.
-- Monkey-patches session_store and session with stubs so we can drive
-- outcomes without a live ring or the C++ optimizer.

local config        = require("vimficiency.config")
local auto_suggest  = require("vimficiency.auto_suggest")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session_store")

local fire_idle      = auto_suggest._for_test.fire_idle
local get_last_fire  = auto_suggest._for_test.get_last_fire_hrtime
local set_last_fire  = auto_suggest._for_test.set_last_fire_hrtime
local reset          = auto_suggest._for_test.reset

-- Save originals so each test starts from a clean slate.
local orig = {
  config_auto_suggest          = config.auto_suggest,
  is_recall_enabled            = session_store.is_recall_enabled,
  get_active                   = session_store.get_active,
  finish_session               = session_store.finish_session,
  summarize                    = session_store.summarize,
  compute_result_for_active    = session.compute_result_for_active,
}

local function restore()
  config.auto_suggest              = orig.config_auto_suggest
  session_store.is_recall_enabled  = orig.is_recall_enabled
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
  session_store.is_recall_enabled = function() return true end
  session_store.get_active = function() return { id = "fake-id" } end
  session_store.finish_session = function() return true end
  session_store.summarize = function()
    return {
      id = "fake-id", kind = "recall",
      result = {
        start_row = 0, start_col = 0, end_row = 0, end_col = 0,
        user_seq = "x", user_cost = 0.0,
        optimal_results = {},
      },
    }
  end
end

test("fire_idle: no-op when recall disabled", function()
  setup_enabled()
  session_store.is_recall_enabled = function() return false end
  fire_idle()
  assert_eq(get_last_fire(), 0, "recall-disabled should not advance cooldown")
  restore()
end)

test("fire_idle: no-op when window unresolved (ring too young)", function()
  setup_enabled()
  session_store.get_active = function() return nil end
  fire_idle()
  assert_eq(get_last_fire(), 0, "unresolved window should not advance cooldown")
  restore()
end)

test("fire_idle: compute failure advances cooldown (throttle)", function()
  setup_enabled()
  session.compute_result_for_active = function() return nil, "rejected" end
  fire_idle()
  assert_true(get_last_fire() > 0,
    "compute failure must set last_fire_hrtime so retries respect cooldown")
  restore()
end)

test("fire_idle: finish failure also advances cooldown", function()
  setup_enabled()
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  session_store.finish_session = function() return false end
  fire_idle()
  assert_true(get_last_fire() > 0,
    "store failure must set last_fire_hrtime so retries respect cooldown")
  restore()
end)

test("fire_idle: success advances cooldown", function()
  setup_enabled()
  session.compute_result_for_active = function()
    return {
      start_row = 0, start_col = 0, end_row = 0, end_col = 0,
      user_seq = "x", optimal_results = { { seq = "y", cost = 1.0 } },
    }
  end
  fire_idle()
  assert_true(get_last_fire() > 0, "success should advance cooldown")
  restore()
end)

test("fire_idle: cooldown short-circuits re-entry", function()
  setup_enabled()
  local compute_calls = 0
  session.compute_result_for_active = function()
    compute_calls = compute_calls + 1
    return nil, "rejected"
  end
  -- Prime cooldown: say we fired 1 ms ago (well inside the 5000 ms window).
  set_last_fire(vim.uv.hrtime() - 1000000)
  fire_idle()
  assert_eq(compute_calls, 0, "cooldown should prevent compute call")
  restore()
end)
