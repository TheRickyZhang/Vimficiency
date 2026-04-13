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
