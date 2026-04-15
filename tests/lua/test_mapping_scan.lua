-- tests/lua/test_mapping_scan.lua
-- Covers scan_rhs_for_vimfy. Its job is a startup safety net: detect
-- user mappings whose RHS invokes :Vimfy / :Vimficiency as a raw Ex
-- command so we can warn about the LHS keystroke being double-counted.
-- The prior `[fy]` character-class pattern false-matched on unrelated
-- commands (`:vimfoo`, `:vimyak`); this test pins both the intended
-- matches and the rejections.

local init = require("vimficiency")
local scan = init._for_test.scan_rhs_for_vimfy

assert_true(type(scan) == "function",
  "init._for_test.scan_rhs_for_vimfy must be exposed for this test")

test("scan: matches :Vimfy at RHS start", function()
  assert_eq(scan(":Vimfy start a<CR>"), true)
  assert_eq(scan(":Vimfy end a"),       true)
  assert_eq(scan("  :Vimfy suggest on"), true, "leading whitespace tolerated")
end)

test("scan: matches :Vimficiency at RHS start", function()
  assert_eq(scan(":Vimficiency start a<CR>"), true)
  assert_eq(scan(":Vimficiency save @ quick"), true)
end)

test("scan: matches <Cmd>Vimfy inline (with and without colon)", function()
  assert_eq(scan("<Cmd>Vimfy end a<CR>"),  true)
  assert_eq(scan("<Cmd>:Vimfy end a<CR>"), true)
  assert_eq(scan("<cmd>vimfy end<CR>"),    true, "case-insensitive")
end)

test("scan: matches <Cmd>Vimficiency inline", function()
  assert_eq(scan("<Cmd>Vimficiency start a<CR>"),  true)
  assert_eq(scan("<Cmd>:Vimficiency list<CR>"),    true)
end)

test("scan: case-insensitive on command name", function()
  assert_eq(scan(":VIMFY start a"),       true)
  assert_eq(scan(":VIMFICIENCY help"),    true)
  assert_eq(scan("<CMD>VIMFY END A<CR>"), true)
end)

test("scan: does NOT match :vimfoo / :vimfrobnicate (old false positives)", function()
  assert_eq(scan(":vimfoo<CR>"),          false,
    "'vimfoo' must not match — old [fy] class false-positived")
  assert_eq(scan(":vimfrobnicate<CR>"),   false)
  assert_eq(scan(":Vimfoobar arg"),       false)
end)

test("scan: does NOT match :vimy... (old false positive on 'y')", function()
  assert_eq(scan(":vimyak<CR>"),   false,
    "'vimyak' must not match — old [fy] class false-positived")
  assert_eq(scan(":Vimyard arg"),  false)
end)

test("scan: does NOT match unrelated commands", function()
  assert_eq(scan(":set number"),                  false)
  assert_eq(scan(":Vim"),                         false, "plain :Vim is not :Vimfy")
  assert_eq(scan("<Cmd>set cursorline<CR>"),      false)
  assert_eq(scan("gq"),                           false, "no colon, no <Cmd>")
end)

test("scan: rejects non-string and empty input", function()
  assert_eq(scan(""),          false)
  assert_eq(scan(nil),         false)
  assert_eq(scan(42),          false)
  assert_eq(scan({ "x" }),     false)
end)

test("scan: <Cmd>Vimfoo must not match", function()
  -- The <Cmd> form was the second regex; regression-pin it too.
  assert_eq(scan("<Cmd>Vimfoo arg<CR>"), false)
  assert_eq(scan("<Cmd>:Vimyak<CR>"),    false)
end)
