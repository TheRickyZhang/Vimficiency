-- tests/lua/auto_suggest.lua
-- Unit tests for fire_idle's dedup/failure-throttle behavior. The actual
-- cooldown timer is owned by end_trigger.arm_idle; fire_idle communicates
-- with it by returning a boolean (true = "this fire counted, refresh
-- cooldown"; false = "no-op, don't lock out the next tick").
-- Monkey-patches session_store and session with stubs so we can drive
-- outcomes without a live queue or the C++ optimizer.

local config        = require("vimficiency.config")
local auto_suggest  = require("vimficiency.capture.auto_suggest")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

local fire_idle   = auto_suggest._for_test.fire_idle
local fire_keys   = auto_suggest._for_test.fire_keys
local fire_cost   = auto_suggest._for_test.fire_cost
local render      = auto_suggest._for_test.render_suggestion
local get_last_fp = auto_suggest._for_test.get_last_fingerprint
local reset       = auto_suggest._for_test.reset

local IDLE_CFG = { idle = { ms = 200, window = "3s" }, cooldown_ms = 5000 }
local KEYS_CFG = { keys = { every = 30 }, cooldown_ms = 5000 }
local COST_M2  = { cost = { m = 2.0, b = 0.0, ms = 300, window = "100" }, cooldown_ms = 5000 }
local COST_M1  = { cost = { m = 1.0, b = 0.0, ms = 300, window = "100" }, cooldown_ms = 5000 }

local ACTIVE = { id = "fake-id", start_kind = "auto", end_kind = "manual" }

local SUMMARY = {
  id = "fake-id",
  type = "suggest", start_kind = "auto", end_kind = "auto",
  result = h.fake_result({ user_cost = 0.0, optimal_results = {} }),
}

--- Run `fn` with auto_suggest stubs installed. `spec` keys:
---   cfg     — config.auto_suggest value (default IDLE_CFG)
---   compute — session.compute_result_for_active stub (default returns fake_result())
---   finish  — session_store.finish_session stub (default no-op true)
---   active  — session_store.get_active stub (default returns ACTIVE)
local function harness(spec, fn)
  spec = spec or {}
  reset()
  local patches = {
    { config, "auto_suggest", spec.cfg or IDLE_CFG },
    { session_store, "get_active",     spec.active  or function() return ACTIVE end },
    { session_store, "finish_session", spec.finish  or function() return true end },
    { session_store, "summarize",      function() return SUMMARY end },
    { session,       "compute_result_for_active",
        spec.compute or function() return h.fake_result() end },
  }
  h.with_patch(patches, fn)
end

--- Helper for reason-capture tests.
local function capture_reason(cfg, compute, trigger)
  local captured
  harness({
    cfg = cfg,
    compute = compute,
    finish = function(...)
      captured = select(5, ...)
      return true
    end,
  }, trigger)
  return captured
end

--------------------------------------------------------------------------------
-- fire_idle
--------------------------------------------------------------------------------

test("fire_idle: no-op when window unresolved (queue too young)", function()
  harness({ active = function() return nil end }, function()
    assert_eq(fire_idle(), false, "unresolved window should not count")
  end)
end)

test("fire_idle: compute and finish failures still count", function()
  harness({ compute = function() return nil, "rejected" end }, function()
    assert_eq(fire_idle(), true,
      "compute failure must count as a fire so retries respect cooldown")
  end)
  harness({ finish = function() return false end }, function()
    assert_eq(fire_idle(), true,
      "store failure must count as a fire so retries respect cooldown")
  end)
end)

test("fire_idle: success counts and records fingerprint", function()
  harness({}, function()
    assert_eq(fire_idle(), true, "success should count")
    assert_true(get_last_fp() ~= nil, "success should record a fingerprint")
  end)
end)

test("fire_idle: promotes end_kind from manual to auto on takeover", function()
  local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  harness({
    active = function() return captured end,
    -- Mirror finish_session's real contract: the end_kind override is
    -- applied atomically with the status transition. A failing finish
    -- leaves end_kind untouched.
    finish = function(...)
      local override = select(4, ...)
      if override then captured.end_kind = override end
      return true
    end,
  }, function()
    fire_idle()
    assert_eq(captured.end_kind, "auto",
      "Recall -> Suggest promotion must flip end_kind at finish time")
    assert_eq(captured.start_kind, "auto",
      "start_kind must stay 'auto' (Suggest is still auto-start)")
  end)
end)

test("fire_idle: failures leave end_kind = 'manual'", function()
  for _, spec in ipairs({
    {
      label = "compute failure",
      patch = { compute = function() return nil, "rejected" end },
    },
    {
      label = "finish failure",
      patch = { finish = function() return false end },
    },
  }) do
    local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
    spec.patch.active = function() return captured end
    harness(spec.patch, function()
      fire_idle()
      assert_eq(captured.end_kind, "manual",
        spec.label .. " must not mutate end_kind")
    end)
  end
end)

--------------------------------------------------------------------------------
-- fire_keys: derives window from cfg.keys.every
--------------------------------------------------------------------------------

test("fire_keys: passes tostring(every) as the recall window", function()
  local captured_alias
  harness({
    cfg = KEYS_CFG,
    active = function(alias) captured_alias = alias; return ACTIVE end,
  }, function()
    fire_keys()
    assert_eq(captured_alias, "30",
      "fire_keys resolves its window from cfg.keys.every (no separate knob)")
  end)
end)

test("fire_keys: no-op when cfg.keys is absent", function()
  harness({}, function()
    assert_eq(fire_keys(), false, "unconfigured trigger must not count")
  end)
end)

test("fire_keys: success path records fingerprint (shared state with fire_idle)", function()
  harness({ cfg = { keys = { every = 50 }, cooldown_ms = 5000 } }, function()
    assert_eq(fire_keys(), true)
    assert_true(get_last_fp() ~= nil,
      "keys trigger must write to the same last_fingerprint as idle")
  end)
end)

--------------------------------------------------------------------------------
-- fire_cost: gate on user_cost vs m*optimal+b
--------------------------------------------------------------------------------

test("fire_cost: gate failure counts but does not fingerprint (user was efficient)", function()
  -- 5 > 2*10+0 == 20 → false → gate fails → suppress notify, still count.
  harness({
    cfg = COST_M2,
    compute = function() return h.fake_result({
      user_cost = 5,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true, "gate failure must still count (optimizer ran)")
    assert_eq(get_last_fp(), nil,
      "gate failure must not record fingerprint — nothing was surfaced")
  end)
end)

test("fire_cost: gate pass records fingerprint", function()
  -- 20 > 1*10+0 == 10 → true → gate passes.
  harness({
    cfg = COST_M1,
    compute = function() return h.fake_result({
      user_cost = 20,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true)
    assert_true(get_last_fp() ~= nil,
      "gate pass must fingerprint (the shown suggestion)")
  end)
end)

test("fire_cost: empty optimal_results suppresses notify", function()
  harness({
    cfg = COST_M1,
    compute = function() return h.fake_result({ user_cost = 20, optimal_results = {} }) end,
  }, function()
    assert_eq(fire_cost(), true, "optimizer ran, still count")
    assert_eq(get_last_fp(), nil,
      "no optimal result to compare against → suppress")
  end)
end)

--------------------------------------------------------------------------------
-- Reason plumbing: each trigger must pass its literal reason through to
-- finish_session so the header can print it.
--------------------------------------------------------------------------------

test("auto_suggest triggers pass literal finish reasons", function()
  assert_eq(capture_reason(IDLE_CFG, nil, fire_idle), "suggest_idle")
  assert_eq(capture_reason(KEYS_CFG, nil, fire_keys), "suggest_keys")
  local compute = function() return h.fake_result({
    user_cost = 20,
    optimal_results = { { seq = "y", cost = 10.0 } },
  }) end
  assert_eq(capture_reason(COST_M1, compute, fire_cost), "suggest_cost")
end)

test("fire_cost: gate fail finishes the record (consume, not leak)", function()
  local finish_called = false
  local captured = { id = "fake-id", start_kind = "auto", end_kind = "manual" }
  harness({
    cfg = COST_M2,
    active = function() return captured end,
    compute = function() return h.fake_result({
      user_cost = 5,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }) end,
    finish = function() finish_called = true; return true end,
  }, function()
    fire_cost()
    assert_eq(finish_called, true,
      "gate failure must still finish the record so next tick doesn't re-analyze it")
  end)
end)

--------------------------------------------------------------------------------
-- Turnkey default: `:Vimfy suggest on` with no configured triggers falls back
-- to the built-in cost gate (DEFAULT_COST = m 1.5, b 2.0).
--------------------------------------------------------------------------------

-- Mirrors a normalized config: no explicit trigger, but default_cost is always
-- present (config_detail fills it from defaults).
local NO_TRIGGERS = {
  cooldown_ms = 5000,
  default_cost = { m = 1.5, b = 2.0, ms = 1000, window = "3s" },
}

test("fire_cost: uses default gate when cfg.cost absent", function()
  harness({
    cfg = NO_TRIGGERS,
    compute = function() return h.fake_result({
      user_cost = 100,
      optimal_results = { { seq = "y", cost = 1.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true, "optimizer ran")
    assert_true(get_last_fp() ~= nil,
      "100 > 1.5*1+2 = 3.5 → default gate passes")
  end)
end)

test("fire_cost: default gate suppresses near-optimal sequences", function()
  harness({
    cfg = NO_TRIGGERS,
    compute = function() return h.fake_result({
      user_cost = 3,
      optimal_results = { { seq = "y", cost = 1.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true, "optimizer ran")
    assert_eq(get_last_fp(), nil,
      "3 <= 1.5*1+2 = 3.5 → default gate suppresses")
  end)
end)

test("enable: turnkey default arms a trigger with no config", function()
  auto_suggest.disable()  -- clean baseline regardless of prior tests
  h.with_patch({ { config, "auto_suggest", NO_TRIGGERS } }, function()
    assert_eq(auto_suggest.is_enabled(), false)
    assert_eq(auto_suggest.enable(), true,
      "enable must arm the default cost gate with no configured triggers")
    assert_eq(auto_suggest.is_enabled(), true)
    auto_suggest.disable()
    assert_eq(auto_suggest.is_enabled(), false)
  end)
end)

--------------------------------------------------------------------------------
-- render_suggestion: notif_cooldown_ms throttles visible toasts. A suppressed
-- toast does not advance last_notify_hrtime; only a shown one does.
--------------------------------------------------------------------------------

local SUGGEST_SUMMARY = {
  result = h.fake_result({
    user_cost = 20,
    optimal_results = { { seq = "ciwx", cost = 1.0 } },
  }),
}

test("render_suggestion: suppresses a toast within notif_cooldown_ms", function()
  local count = 0
  h.with_patch({
    { config, "auto_suggest", { notif_cooldown_ms = 600000 } },
    { vim, "notify", function() count = count + 1 end },
  }, function()
    reset()
    render(SUGGEST_SUMMARY)
    render(SUGGEST_SUMMARY)
    assert_eq(count, 1, "second toast within cooldown must be suppressed")
  end)
end)

test("render_suggestion: notif_cooldown_ms = 0 never suppresses", function()
  local count = 0
  h.with_patch({
    { config, "auto_suggest", { notif_cooldown_ms = 0 } },
    { vim, "notify", function() count = count + 1 end },
  }, function()
    reset()
    render(SUGGEST_SUMMARY)
    render(SUGGEST_SUMMARY)
    assert_eq(count, 2, "no cooldown → both toasts show")
  end)
end)
