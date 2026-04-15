-- tests/lua/test_config_validation.lua
-- Guards the "loud on typo" contract for nested config keys.
-- Requires libvimficiency.so to be built — if load fails the whole
-- suite fails loudly, which is the correct signal in CI.

local ok_ffi, ffi_lib = pcall(require, "vimficiency.ffi")
if not ok_ffi then
  error("vimficiency.ffi failed to load (is the C++ library built?): " .. tostring(ffi_lib))
end

local function expects_error(fn, pattern, msg)
  local ok, err = pcall(fn)
  if ok then
    error((msg or "expected error") .. ": call succeeded", 2)
  end
  if pattern and not tostring(err):find(pattern, 1, true) then
    error(string.format("%s: error message %q did not contain %q",
      msg or "expected error", tostring(err), pattern), 2)
  end
end

test("configure: unknown key in weights raises", function()
  expects_error(
    function() ffi_lib.configure({ weights = { downwrad = 5 } }) end,
    "weights.downwrad",
    "typo in weights must error"
  )
end)

test("configure: known keys in weights succeed", function()
  -- keyWeight exists on C_ScoreWeights. If the binding changes, this
  -- test's positive assertion needs adjusting alongside.
  local ok, err = pcall(function()
    ffi_lib.configure({ weights = { keyWeight = 1.0 } })
  end)
  if not ok then
    error("valid weights key rejected: " .. tostring(err))
  end
end)

test("configure: unknown key in count_penalty_overrides[class] raises", function()
  -- Look up a real CountClass enum name so the class_index resolves and
  -- we hit the per-field validator (not the "Unknown count penalty
  -- class" branch).
  local class_name
  for name, _ in pairs(ffi_lib.CountClass) do class_name = name; break end
  assert_true(class_name, "CountClass enum must expose at least one name")

  expects_error(
    function()
      ffi_lib.configure({
        count_penalty_overrides = { [class_name] = { base_slope = 1.0 } },
      })
    end,
    "base_slope",
    "typo in count_penalty_overrides must error"
  )
end)

test("configure: valid count_penalty_overrides accepted", function()
  local class_name
  for name, _ in pairs(ffi_lib.CountClass) do class_name = name; break end
  local ok, err = pcall(function()
    ffi_lib.configure({
      count_penalty_overrides = { [class_name] = { base = 1.0, count_slope = 0.5 } },
    })
  end)
  if not ok then
    error("valid count_penalty_overrides key rejected: " .. tostring(err))
  end
end)

test("configure: unknown count penalty class raises (existing behavior)", function()
  expects_error(
    function()
      ffi_lib.configure({
        count_penalty_overrides = { FakeClass = { base = 1.0 } },
      })
    end,
    "Unknown count penalty class",
    "bogus class name must error"
  )
end)

--------------------------------------------------------------------------------
-- Lua-side validator shape: watch and auto_suggest (parallel nested `idle`)
--------------------------------------------------------------------------------

local config         = require("vimficiency.config")
local init           = require("vimficiency.init")
local validate_watch = init._for_test.validate_watch
local validate_auto_suggest = init._for_test.validate_auto_suggest

local function with_saved_watch(fn)
  local prev = config.watch
  local ok, err = pcall(fn)
  config.watch = prev
  if not ok then error(err, 2) end
end

local function with_saved_auto_suggest(fn)
  local prev = config.auto_suggest
  local ok, err = pcall(fn)
  config.auto_suggest = prev
  if not ok then error(err, 2) end
end

test("validate_watch: flat idle_ms (legacy shape) errors loudly", function()
  with_saved_watch(function()
    expects_error(
      function() validate_watch({ idle_ms = 3000, cooldown_ms = 5000 }) end,
      "idle_ms",
      "flat idle_ms must be rejected"
    )
  end)
end)

test("validate_watch: idle.window is rejected with explanatory message", function()
  with_saved_watch(function()
    expects_error(
      function() validate_watch({ idle = { ms = 3000, window = "3s" } }) end,
      "window is not allowed",
      "watch.idle.window must be rejected"
    )
  end)
end)

test("validate_watch: nested idle.ms shape is accepted", function()
  with_saved_watch(function()
    validate_watch({ idle = { ms = 3000 }, cooldown_ms = 5000 })
    assert_eq(config.watch.idle.ms, 3000)
    assert_eq(config.watch.cooldown_ms, 5000)
  end)
end)

test("validate_watch: non-table idle errors", function()
  with_saved_watch(function()
    expects_error(
      function() validate_watch({ idle = 3000 }) end,
      "watch.idle must be a table",
      "scalar idle must be rejected"
    )
  end)
end)

test("validate_auto_suggest: idle requires an explicit window", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ idle = { ms = 3000 }, cooldown_ms = 5000 }) end,
      "auto_suggest.idle.window",
      "present idle trigger must be fully specified"
    )
  end)
end)

test("validate_auto_suggest: explicit idle window is accepted", function()
  with_saved_auto_suggest(function()
    validate_auto_suggest({ idle = { ms = 3000, window = "50" } })
    assert_eq(config.auto_suggest.idle.ms, 3000)
    assert_eq(config.auto_suggest.idle.window, "50",
      "idle trigger should preserve the explicit window")
  end)
end)

test("validate_auto_suggest: invalid window still errors", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ idle = { ms = 3000, window = "3m" } }) end,
      "recall alias",
      "non-recall-alias window must error"
    )
  end)
end)

test("validate_auto_suggest: keys trigger accepts a positive integer", function()
  with_saved_auto_suggest(function()
    validate_auto_suggest({ keys = { every = 50 } })
    assert_eq(config.auto_suggest.keys.every, 50)
    assert_eq(config.auto_suggest.cooldown_ms, 5000,
      "feature cooldown should still default when only keys is configured")
  end)
end)

test("validate_auto_suggest: cost requires every field", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ cost = { m = 1.5, b = 2.0 } }) end,
      "auto_suggest.cost.ms",
      "present cost trigger must be fully specified"
    )
  end)
end)

--------------------------------------------------------------------------------
-- validate_auto_suggest: keys trigger edge cases
--------------------------------------------------------------------------------

test("validate_auto_suggest: keys.every must be a positive integer", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ keys = { every = 0 } }) end,
      "positive integer",
      "every=0 must be rejected"
    )
    expects_error(
      function() validate_auto_suggest({ keys = { every = 1.5 } }) end,
      "positive integer",
      "non-integer every must be rejected"
    )
  end)
end)

test("validate_auto_suggest: keys with unknown sub-key errors", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ keys = { every = 50, window = "50" } }) end,
      "unknown key",
      "window inside keys has no meaning (collapses to every)"
    )
  end)
end)

--------------------------------------------------------------------------------
-- validate_auto_suggest: cost trigger edge cases
--------------------------------------------------------------------------------

test("validate_auto_suggest: cost with explicit ms and window", function()
  with_saved_auto_suggest(function()
    validate_auto_suggest({
      cost = { m = 1.5, b = 2.0, ms = 500, window = "30s" },
    })
    assert_eq(config.auto_suggest.cost.ms, 500)
    assert_eq(config.auto_suggest.cost.window, "30s")
  end)
end)

test("validate_auto_suggest: cost.m must be positive", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ cost = { m = 0, b = 2.0, ms = 500, window = "30s" } }) end,
      "positive",
      "m=0 must be rejected"
    )
  end)
end)

test("validate_auto_suggest: cost.b must be non-negative", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ cost = { m = 1.5, b = -1, ms = 500, window = "30s" } }) end,
      "non-negative",
      "b<0 must be rejected"
    )
  end)
end)

test("validate_auto_suggest: cost.window must be a recall alias", function()
  with_saved_auto_suggest(function()
    expects_error(
      function()
        validate_auto_suggest({ cost = { m = 1.5, b = 2.0, ms = 500, window = "3m" } })
      end,
      "recall alias",
      "bogus window must be rejected"
    )
  end)
end)

test("validate_auto_suggest: cost with unknown sub-key errors", function()
  with_saved_auto_suggest(function()
    expects_error(
      function()
        validate_auto_suggest({ cost = { m = 1.5, b = 2.0, ms = 500, window = "30s", slope = 0.1 } })
      end,
      "unknown key",
      "cost must reject unknown sub-keys"
    )
  end)
end)

-- validate_auto_suggest: trigger presence is required
--------------------------------------------------------------------------------

test("validate_auto_suggest: empty table (no triggers) errors loudly", function()
  with_saved_auto_suggest(function()
    expects_error(
      function() validate_auto_suggest({ cooldown_ms = 5000 }) end,
      "must configure at least one trigger",
      "cooldown_ms alone is not a valid feature config"
    )
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
