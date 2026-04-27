-- tests/lua/mapping_scan.lua
-- Covers scan_rhs_for_vimfy. Its job is a startup safety net: detect
-- user mappings whose RHS invokes :Vimfy / :Vimficiency as a raw Ex
-- command so we can warn about the LHS keystroke being double-counted.
-- The prior `[fy]` character-class pattern false-matched on unrelated
-- commands (`:vimfoo`, `:vimyak`); this test pins both the intended
-- matches and the rejections.

local scan = require("vimficiency.mapping_scan").scan_rhs_for_vimfy

test("scan: matches raw Ex and <Cmd> Vimfy commands", function()
  for _, rhs in ipairs({
    ":Vimfy start a<CR>",
    ":Vimfy finish a",
    "  :Vimfy suggest on",
    ":Vimficiency start a<CR>",
    ":Vimficiency save @ quick",
    "<Cmd>Vimfy finish a<CR>",
    "<Cmd>:Vimfy finish a<CR>",
    "<cmd>vimfy finish<CR>",
    "<Cmd>Vimficiency start a<CR>",
    "<Cmd>:Vimficiency list<CR>",
    ":VIMFY help",
    ":VIMFICIENCY help",
    "<CMD>VIMFY FINISH A<CR>",
  }) do
    assert_eq(scan(rhs), true, rhs)
  end
end)

test("scan: rejects Vim-like command prefixes", function()
  for _, rhs in ipairs({
    ":vimfoo<CR>",
    ":vimfrobnicate<CR>",
    ":Vimfoobar arg",
    ":vimyak<CR>",
    ":Vimyard arg",
    "<Cmd>Vimfoo arg<CR>",
    "<Cmd>:Vimyak<CR>",
  }) do
    assert_eq(scan(rhs), false, rhs)
  end
end)

test("scan: rejects unrelated or malformed input", function()
  for _, rhs in ipairs({
    ":set number",
    ":Vim",
    "<Cmd>set cursorline<CR>",
    "gq",
    "",
  }) do
    assert_eq(scan(rhs), false, rhs)
  end
  assert_eq(scan(nil), false)
  assert_eq(scan(42), false)
  assert_eq(scan({ "x" }), false)
end)
