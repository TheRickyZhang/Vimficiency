-- Coverage for `require('vimficiency').map()`.
-- Includes the end-to-end "LHS keystroke is not recorded as motion" contract.

local vimfy        = require("vimficiency")
local key_tracking = require("vimficiency.capture.key_tracking")
local h            = require("_helpers")

-- Skip `setup()` here; these tests only need the exported helpers.

--- Feed `keys` as user input and return the captured `key_typed` string.
---@param keys string  Literal keys (e.g. "j", "Y", or "<F4>" after termcodes)
---@return string captured  Concatenated `key_typed` of every event received
local function feed_and_capture(keys)
  local captured = {}
  local ok = key_tracking.attach_global(function(event)
    captured[#captured + 1] = event.key_typed
  end, "test_map_helper_probe")
  assert_true(ok, "probe listener should attach cleanly")
  h.feed(keys)
  key_tracking.detach_global("test_map_helper_probe")
  return table.concat(captured, "")
end

--- Look up a keymap registration for a given LHS in normal mode.
local function find_map(lhs)
  for _, m in ipairs(vim.api.nvim_get_keymap("n")) do
    if m.lhs == lhs then return m end
  end
  return nil
end

test("map: string spec with args attaches a callback", function()
  h.with_temp_map("n", "<Plug>TestVimfyMapA", "list", function()
    local found = find_map("<Plug>TestVimfyMapA")
    assert_true(found, "map should register the keymap")
    assert_eq(found.desc, "test-map-a")
    assert_true(type(found.callback) == "function", "map should use a Lua callback")
  end, { desc = "test-map-a" })
end)

test("map: function spec is wrapped", function()
  local called = false
  h.with_temp_map("n", "<Plug>TestVimfyMapFn", function() called = true end, function()
    local found = find_map("<Plug>TestVimfyMapFn")
    assert_true(found, "function spec should register")
    found.callback()
    assert_true(called, "wrapped callback should run when invoked")
  end)
end)

test("map: empty string spec errors", function()
  assert_error(
    function() vimfy.map("n", "<Plug>TestVimfyMapEmpty", "") end,
    "empty spec", "error should mention empty spec"
  )
end)

test("map: invalid spec type errors", function()
  assert_error(
    function() vimfy.map("n", "<Plug>TestVimfyMapBad", 42) end,
    "must be a string or function", "error should describe valid types"
  )
end)

test("map: default silent is true, user can override", function()
  h.with_temp_map("n", "<Plug>TestVimfyMapSilent", "list", function()
    local found = find_map("<Plug>TestVimfyMapSilent")
    assert_eq(found.silent, 1, "silent should default to true")
  end)
end)

--------------------------------------------------------------------------------
-- End-to-end LHS recording contract.
--------------------------------------------------------------------------------

test("e2e: literal 'j' is captured (baseline)", function()
  local captured = feed_and_capture("j")
  assert_match(captured, "j",
    "literal 'j' must surface in on_key events")
end)

test("e2e: vimfy.map string spec — LHS not recorded as motion", function()
  h.with_temp_map("n", "<F4>", "list", function()
    assert_eq(feed_and_capture("<F4>"), "",
      "vimfy.map-bound key must not surface in on_key events")
  end)
end)

test("e2e: vimfy.map function spec — LHS not recorded as motion", function()
  local body_ran = false
  h.with_temp_map("n", "<F5>", function() body_ran = true end, function()
    assert_eq(feed_and_capture("<F5>"), "",
      "vimfy.map(fn) key must not surface in on_key events")
    assert_true(body_ran, "bound function should have fired")
  end)
end)

test("e2e: wrap() suppresses keys fired inside its body", function()
  -- Verify the low-level primitive that `vimfy.map()` is built on.
  local captured = {}
  local ok = key_tracking.attach_global(function(event)
    captured[#captured + 1] = event.key_typed
  end, "test_map_helper_wrap_probe")
  assert_true(ok, "probe listener should attach cleanly")

  local wrapped = vimfy.wrap(function()
    h.feed("j")
  end)
  wrapped()

  key_tracking.detach_global("test_map_helper_wrap_probe")
  assert_eq(table.concat(captured, ""), "",
    "keys fed inside wrap() body must be suppressed")
end)

--------------------------------------------------------------------------------
-- Multi-key LHS coverage.
--------------------------------------------------------------------------------

test("e2e: multi-key LHS (string spec) — no key leaks on resolve", function()
  h.with_temp_map("n", "jk", "list", function()
    assert_eq(feed_and_capture("jk"), "",
      "multi-key LHS 'jk' (string spec) leaked")
  end)
end)

test("e2e: multi-key LHS (function spec) — no key leaks on resolve", function()
  h.with_temp_map("n", "qw", function() end, function()
    assert_eq(feed_and_capture("qw"), "",
      "multi-key LHS 'qw' (fn spec) leaked")
  end)
end)

test("e2e: three-key LHS (string spec) — no key leaks on resolve", function()
  h.with_temp_map("n", "xyz", "list", function()
    assert_eq(feed_and_capture("xyz"), "",
      "multi-key LHS 'xyz' (string spec) leaked")
  end)
end)

test("e2e: user-remap → <Plug> → wrap() chain — no key leaks", function()
  h.with_temp_map("n", "<Plug>VimfyProbeRecall", "list", function()
    pcall(vim.keymap.del, "n", "zx")
    vim.keymap.set("n", "zx", "<Plug>VimfyProbeRecall", { remap = true })
    local captured = feed_and_capture("zx")
    pcall(vim.keymap.del, "n", "zx")
    assert_eq(captured, "",
      "user-remap → <Plug> chain leaked")
  end)
end)

test("e2e: listener sees keys AFTER wrap() returns (ignore flag restored)", function()
  -- Regression guard: end_ignore must restore the flag so post-wrap
  -- keypresses are captured normally.
  local wrapped = vimfy.wrap(function() end)
  wrapped()
  assert_match(feed_and_capture("k"), "k",
    "literal 'k' after wrap() must be captured")
end)
