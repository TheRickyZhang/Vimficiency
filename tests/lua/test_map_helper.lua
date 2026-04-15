-- tests/lua/test_map_helper.lua
-- Covers `require('vimficiency').map()`: registration shape plus the
-- end-to-end "LHS keystroke is not recorded as motion" contract,
-- driven by nvim_feedkeys('xt', ...) against a real global listener.

local vimfy = require("vimficiency")
local key_tracking = require("vimficiency.key_tracking")

-- Don't call setup() here — it registers user commands and may run the
-- scan. We only need the module exports: map, wrap.

--- Attach a capture listener, feed `keys` as if typed by a user, and
--- return the keytrans'd sequence recorded by on_key. Uses 'xt' so
--- nvim_feedkeys flushes synchronously with a non-empty `typed` field
--- (plain 'x' fires with typed="" and we'd see nothing).
---@param keys string  Literal keys (e.g. "j", "Y", or "<F4>" after termcodes)
---@return string captured  Concatenated `key_typed` of every event received
local function feed_and_capture(keys)
  local captured = {}
  local ok = key_tracking.attach_global(function(event)
    captured[#captured + 1] = event.key_typed
  end, "test_map_helper_probe")
  assert_true(ok, "probe listener should attach cleanly")
  local termkeys = vim.api.nvim_replace_termcodes(keys, true, false, true)
  vim.api.nvim_feedkeys(termkeys, "xt", false)
  key_tracking.detach_global("test_map_helper_probe")
  return table.concat(captured, "")
end

test("map: string spec with args attaches a callback", function()
  pcall(vim.keymap.del, "n", "<Plug>TestVimfyMapA")
  -- Spec doesn't need to match a real subcommand for the registration
  -- check — failures surface only when the mapping fires. We use a
  -- known subcommand (`list`) so it doesn't warn on invocation.
  vimfy.map("n", "<Plug>TestVimfyMapA", "list", { desc = "test-map-a" })

  local maps = vim.api.nvim_get_keymap("n")
  local found
  for _, m in ipairs(maps) do
    if m.lhs == "<Plug>TestVimfyMapA" then found = m; break end
  end
  assert_true(found, "map should register the keymap")
  assert_eq(found.desc, "test-map-a")
  assert_true(type(found.callback) == "function", "map should use a Lua callback")
  pcall(vim.keymap.del, "n", "<Plug>TestVimfyMapA")
end)

test("map: function spec is wrapped", function()
  pcall(vim.keymap.del, "n", "<Plug>TestVimfyMapFn")
  local called = false
  vimfy.map("n", "<Plug>TestVimfyMapFn", function()
    called = true
  end)
  local maps = vim.api.nvim_get_keymap("n")
  local found
  for _, m in ipairs(maps) do
    if m.lhs == "<Plug>TestVimfyMapFn" then found = m; break end
  end
  assert_true(found, "function spec should register")
  -- Fire the callback directly; wrap returns a callable.
  found.callback()
  assert_true(called, "wrapped callback should run when invoked")
  pcall(vim.keymap.del, "n", "<Plug>TestVimfyMapFn")
end)

test("map: empty string spec errors", function()
  local ok, err = pcall(vimfy.map, "n", "<Plug>TestVimfyMapEmpty", "")
  assert_true(not ok, "empty spec should error")
  assert_true(tostring(err):find("empty spec", 1, true),
    "error should mention empty spec: " .. tostring(err))
end)

test("map: invalid spec type errors", function()
  local ok, err = pcall(vimfy.map, "n", "<Plug>TestVimfyMapBad", 42)
  assert_true(not ok, "non-string non-function spec should error")
  assert_true(tostring(err):find("must be a string or function", 1, true),
    "error should describe valid types: " .. tostring(err))
end)

test("map: default silent is true, user can override", function()
  pcall(vim.keymap.del, "n", "<Plug>TestVimfyMapSilent")
  vimfy.map("n", "<Plug>TestVimfyMapSilent", "list")
  local maps = vim.api.nvim_get_keymap("n")
  for _, m in ipairs(maps) do
    if m.lhs == "<Plug>TestVimfyMapSilent" then
      assert_eq(m.silent, 1, "silent should default to true")
      break
    end
  end
  pcall(vim.keymap.del, "n", "<Plug>TestVimfyMapSilent")
end)

--------------------------------------------------------------------------------
-- End-to-end: LHS keystroke recording contract
--
-- These tests drive the full filter stack. They confirm that:
--   (1) an ordinary keypress IS captured (baseline — the listener works),
--   (2) a key bound through `vimfy.map()` with a STRING spec is NOT captured
--       (the wrap()'d callback sets the ignore flag around its body),
--   (3) same for a FUNCTION spec.
-- If the announce-only filter ever regresses, these will fail loudly.
--------------------------------------------------------------------------------

test("e2e: literal 'j' is captured (baseline)", function()
  local captured = feed_and_capture("j")
  assert_true(captured:find("j", 1, true),
    "literal 'j' must surface in on_key events; got: " .. vim.inspect(captured))
end)

test("e2e: vimfy.map string spec — LHS not recorded as motion", function()
  pcall(vim.keymap.del, "n", "<F4>")
  vimfy.map("n", "<F4>", "list")
  local captured = feed_and_capture("<F4>")
  assert_eq(captured, "",
    "vimfy.map-bound key must not surface in on_key events; got: " .. vim.inspect(captured))
  pcall(vim.keymap.del, "n", "<F4>")
end)

test("e2e: vimfy.map function spec — LHS not recorded as motion", function()
  pcall(vim.keymap.del, "n", "<F5>")
  local body_ran = false
  vimfy.map("n", "<F5>", function() body_ran = true end)
  local captured = feed_and_capture("<F5>")
  assert_true(body_ran, "bound function should have fired")
  assert_eq(captured, "",
    "vimfy.map(fn) key must not surface in on_key events; got: " .. vim.inspect(captured))
  pcall(vim.keymap.del, "n", "<F5>")
end)

test("e2e: wrap() suppresses keys fired inside its body", function()
  -- `wrap` is the low-level primitive vimfy.map is built on. Verify it in
  -- isolation: calling a wrapped function should suppress any key events
  -- that fire during its body (whether from nested API calls or keymaps
  -- its callees set up).
  local captured = {}
  local ok = key_tracking.attach_global(function(event)
    captured[#captured + 1] = event.key_typed
  end, "test_map_helper_wrap_probe")
  assert_true(ok, "probe listener should attach cleanly")

  local wrapped = vimfy.wrap(function()
    -- Feed a key from inside the wrapped body; it should be suppressed.
    vim.api.nvim_feedkeys(
      vim.api.nvim_replace_termcodes("j", true, false, true), "xt", false)
  end)
  wrapped()

  key_tracking.detach_global("test_map_helper_wrap_probe")
  assert_eq(table.concat(captured, ""), "",
    "keys fed inside wrap() body must be suppressed; got: " .. vim.inspect(captured))
end)

--------------------------------------------------------------------------------
-- Multi-key LHS: does the announce-only filter hold when the user binds
-- a mapping whose LHS is more than one keystroke? The legacy docs
-- (04-recall.md, 12-limitations.md) claim the final key of a multi-key
-- `<Plug>`-mapped LHS "may be captured before the mapping resolves."
-- Nail down the actual behavior under each binding style so we can
-- update the docs to reflect reality rather than hedge.
--
-- If any of these fail with a captured key, the docs were right and the
-- expected string names the leaked char; if they all pass with "", the
-- filter is tighter than the docs suggest and we can delete the hedge.
--------------------------------------------------------------------------------

test("e2e: multi-key LHS (string spec) — no key leaks on resolve", function()
  pcall(vim.keymap.del, "n", "jk")
  vimfy.map("n", "jk", "list")
  local captured = feed_and_capture("jk")
  assert_eq(captured, "",
    "multi-key LHS 'jk' (string spec) leaked: " .. vim.inspect(captured))
  pcall(vim.keymap.del, "n", "jk")
end)

test("e2e: multi-key LHS (function spec) — no key leaks on resolve", function()
  pcall(vim.keymap.del, "n", "qw")
  vimfy.map("n", "qw", function() end)
  local captured = feed_and_capture("qw")
  assert_eq(captured, "",
    "multi-key LHS 'qw' (fn spec) leaked: " .. vim.inspect(captured))
  pcall(vim.keymap.del, "n", "qw")
end)

test("e2e: three-key LHS (string spec) — no key leaks on resolve", function()
  pcall(vim.keymap.del, "n", "xyz")
  vimfy.map("n", "xyz", "list")
  local captured = feed_and_capture("xyz")
  assert_eq(captured, "",
    "multi-key LHS 'xyz' (string spec) leaked: " .. vim.inspect(captured))
  pcall(vim.keymap.del, "n", "xyz")
end)

test("e2e: user-remap → <Plug> → wrap() chain — no key leaks", function()
  -- The exact path 07-keymaps.md describes: define a <Plug> map whose RHS
  -- is a wrap()'d callback, then a user-facing remap that routes through
  -- it. This is what the "known behavior" note in 04-recall.md is about.
  pcall(vim.keymap.del, "n", "<Plug>VimfyProbeRecall")
  pcall(vim.keymap.del, "n", "zx")
  vimfy.map("n", "<Plug>VimfyProbeRecall", "list")
  vim.keymap.set("n", "zx", "<Plug>VimfyProbeRecall", { remap = true })
  local captured = feed_and_capture("zx")
  assert_eq(captured, "",
    "user-remap → <Plug> chain leaked: " .. vim.inspect(captured))
  pcall(vim.keymap.del, "n", "zx")
  pcall(vim.keymap.del, "n", "<Plug>VimfyProbeRecall")
end)

test("e2e: listener sees keys AFTER wrap() returns (ignore flag restored)", function()
  -- Regression guard: end_ignore must restore the flag so post-wrap
  -- keypresses are captured normally.
  local wrapped = vimfy.wrap(function() end)
  wrapped()
  local captured = feed_and_capture("k")
  assert_true(captured:find("k", 1, true),
    "literal 'k' after wrap() must be captured; got: " .. vim.inspect(captured))
end)
