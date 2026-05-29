-- tests/lua/config_validation.lua
-- Guards the "loud on typo" contract for nested config keys.
-- Requires libvimficiency.so to be built — if load fails the whole
-- suite fails loudly, which is the correct signal in CI.

local ok_ffi, ffi_lib = pcall(require, "vimficiency.ffi")
if not ok_ffi then
  error("vimficiency.ffi failed to load (is the C++ library built?): " .. tostring(ffi_lib))
end

local config        = require("vimficiency.config")
local config_detail = require("vimficiency.config_detail")
local h             = require("_helpers")

-- Thin wrappers around the normalizers that also assign the result onto
-- `config`. Matches what the plugin's setup path does on real user input.
local function validate_watch(raw)
  config.watch = config_detail.normalize_watch(raw)
end

local function validate_auto_suggest(raw)
  config.auto_suggest = config_detail.normalize_auto_suggest(raw, config._defaults.auto_suggest)
end

local function with_saved_watch(fn)
  h.with_patch({ { config, "watch", config.watch } }, fn)
end

local function with_saved_auto_suggest(fn)
  h.with_patch({ { config, "auto_suggest", config.auto_suggest } }, fn)
end

local function first_count_class()
  local class_name
  for name, _ in pairs(ffi_lib.CountClass) do class_name = name; break end
  assert_true(class_name, "CountClass enum must expose at least one name")
  return class_name
end

--------------------------------------------------------------------------------
-- ffi_lib.configure: weights + count_penalty_overrides
--------------------------------------------------------------------------------

test("configure: validates weight keys", function()
  assert_error(
    function() ffi_lib.configure({ weights = { downwrad = 5 } }) end,
    "weights.downwrad", "typo in weights must error"
  )
  ffi_lib.configure({ weights = { key_weight = 1.0 } })
end)

test("configure: validates count penalty override shape", function()
  local class_name = first_count_class()
  assert_error(
    function()
      ffi_lib.configure({
        count_penalty_overrides = { [class_name] = { base_slope = 1.0 } },
      })
    end,
    "base_slope", "typo in count_penalty_overrides must error"
  )
  ffi_lib.configure({
    count_penalty_overrides = { [class_name] = { base = 1.0, count_slope = 0.5 } },
  })
  assert_error(
    function()
      ffi_lib.configure({ count_penalty_overrides = { FakeClass = { base = 1.0 } } })
    end,
    "Unknown count penalty class", "bogus class name must error"
  )
end)

--------------------------------------------------------------------------------
-- Lua-side validator shape: watch and auto_suggest (parallel nested `idle`)
--------------------------------------------------------------------------------

test("validate_watch: accepts nested idle.ms and rejects legacy shapes", function()
  with_saved_watch(function()
    assert_error(
      function() validate_watch({ idle_ms = 3000, cooldown_ms = 5000 }) end,
      "idle_ms", "flat idle_ms must be rejected"
    )
    assert_error(
      function() validate_watch({ idle = { ms = 3000, window = "3s" } }) end,
      "window is not allowed", "watch.idle.window must be rejected"
    )
    assert_error(
      function() validate_watch({ idle = 3000 }) end,
      "watch.idle must be a table", "scalar idle must be rejected"
    )
    validate_watch({ idle = { ms = 3000 }, cooldown_ms = 5000 })
    assert_eq(config.watch.idle.ms, 3000)
    assert_eq(config.watch.cooldown_ms, 5000)
  end)
end)

test("validate_auto_suggest: validates idle trigger shape", function()
  with_saved_auto_suggest(function()
    assert_error(
      function() validate_auto_suggest({ idle = { ms = 3000 }, cooldown_ms = 5000 }) end,
      "auto_suggest.idle.window", "present idle trigger must be fully specified"
    )
    assert_error(
      function() validate_auto_suggest({ idle = { ms = 3000, window = "3m" } }) end,
      "recall alias", "non-recall-alias window must error"
    )
    validate_auto_suggest({ idle = { ms = 3000, window = "50" } })
    assert_eq(config.auto_suggest.idle.ms, 3000)
    assert_eq(config.auto_suggest.idle.window, "50",
      "idle trigger should preserve the explicit window")
  end)
end)

test("validate_auto_suggest: validates keys trigger shape", function()
  with_saved_auto_suggest(function()
    assert_error(
      function() validate_auto_suggest({ keys = { every = 0 } }) end,
      "positive integer", "every=0 must be rejected"
    )
    assert_error(
      function() validate_auto_suggest({ keys = { every = 1.5 } }) end,
      "positive integer", "non-integer every must be rejected"
    )
    assert_error(
      function() validate_auto_suggest({ keys = { every = 50, window = "50" } }) end,
      "unknown key", "window inside keys has no meaning"
    )
    validate_auto_suggest({ keys = { every = 50 } })
    assert_eq(config.auto_suggest.keys.every, 50)
    assert_eq(config.auto_suggest.cooldown_ms, 5000,
      "feature cooldown should still default when only keys is configured")
  end)
end)

test("validate_auto_suggest: validates cost trigger shape", function()
  with_saved_auto_suggest(function()
    assert_error(
      function() validate_auto_suggest({ cost = { m = 1.5, b = 2.0 } }) end,
      "auto_suggest.cost.ms", "present cost trigger must be fully specified"
    )
    assert_error(
      function() validate_auto_suggest({ cost = { m = 0, b = 2.0, ms = 500, window = "30s" } }) end,
      "positive", "m=0 must be rejected"
    )
    assert_error(
      function() validate_auto_suggest({ cost = { m = 1.5, b = -1, ms = 500, window = "30s" } }) end,
      "non-negative", "b<0 must be rejected"
    )
    assert_error(
      function() validate_auto_suggest({ cost = { m = 1.5, b = 2.0, ms = 500, window = "3m" } }) end,
      "recall alias", "bogus window must be rejected"
    )
    assert_error(
      function()
        validate_auto_suggest({ cost = { m = 1.5, b = 2.0, ms = 500, window = "30s", slope = 0.1 } })
      end,
      "unknown key", "cost must reject unknown sub-keys"
    )
    validate_auto_suggest({ cost = { m = 1.5, b = 2.0, ms = 500, window = "30s" } })
    assert_eq(config.auto_suggest.cost.ms, 500)
    assert_eq(config.auto_suggest.cost.window, "30s")
  end)
end)

--------------------------------------------------------------------------------
-- validate_auto_suggest: trigger presence is required
--------------------------------------------------------------------------------

test("validate_auto_suggest: empty table (no triggers) errors loudly", function()
  with_saved_auto_suggest(function()
    assert_error(
      function() validate_auto_suggest({ cooldown_ms = 5000 }) end,
      "must configure at least one trigger",
      "cooldown_ms alone is not a valid feature config"
    )
  end)
end)

test("validate_auto_suggest: default_cost always present and tunable without a trigger", function()
  with_saved_auto_suggest(function()
    -- Zero config still yields the fallback gate.
    config.auto_suggest = config_detail.normalize_auto_suggest(nil, config._defaults.auto_suggest)
    assert_eq(config.auto_suggest.default_cost.m, 1.5)
    assert_eq(config.auto_suggest.default_cost.window, "3s")

    -- A partial override is allowed with no trigger and merges over defaults.
    validate_auto_suggest({ default_cost = { m = 3.0 } })
    assert_eq(config.auto_suggest.default_cost.m, 3.0)
    assert_eq(config.auto_suggest.default_cost.b, 2.0,
      "unset default_cost fields fall back to defaults")
  end)
end)

test("validate_auto_suggest: default_cost validates its shape", function()
  with_saved_auto_suggest(function()
    assert_error(
      function() validate_auto_suggest({ default_cost = { m = 0 } }) end,
      "auto_suggest.default_cost.m", "default_cost is validated like a cost trigger"
    )
  end)
end)

test("validate_auto_suggest: notif_cooldown_ms defaults and validates", function()
  with_saved_auto_suggest(function()
    config.auto_suggest = config_detail.normalize_auto_suggest(nil, config._defaults.auto_suggest)
    assert_eq(config.auto_suggest.notif_cooldown_ms, 2000, "default notif cooldown")

    assert_error(
      function()
        validate_auto_suggest({
          cost = { m = 1.5, b = 2.0, ms = 500, window = "30s" },
          notif_cooldown_ms = -1,
        })
      end,
      "notif_cooldown_ms", "negative notif cooldown must be rejected"
    )

    validate_auto_suggest({
      cost = { m = 1.5, b = 2.0, ms = 500, window = "30s" },
      notif_cooldown_ms = 1000,
    })
    assert_eq(config.auto_suggest.notif_cooldown_ms, 1000)
  end)
end)

test("validate_auto_suggest: all three triggers coexist", function()
  with_saved_auto_suggest(function()
    validate_auto_suggest({
      idle = { ms = 3000, window = "3s" },
      keys = { every = 50 },
      cost = { m = 1.5, b = 2.0, ms = 500, window = "30s" },
      cooldown_ms = 5000,
    })
    assert_true(config.auto_suggest.idle ~= nil)
    assert_true(config.auto_suggest.keys ~= nil)
    assert_true(config.auto_suggest.cost ~= nil)
    assert_eq(config.auto_suggest.cooldown_ms, 5000,
      "cooldown_ms is feature-level, applies to all triggers")
  end)
end)
