-- tests/lua/alias.lua
-- Grammar tests for lua/vimficiency/session/alias.lua. Pure-function tests:
-- no buffers, no vim.uv, no state.

local alias = require("vimficiency.session.alias")

test("parse: manual aliases are alphabetic", function()
  for _, input in ipairs({ "a", "refactor", "REFACTOR", "FooBar" }) do
    local t, v = alias.parse(input)
    assert_eq(t, "manual", input)
    assert_eq(v, nil, input)
  end
end)

test("parse: recall aliases carry numeric values", function()
  for _, case in ipairs({
    { input = "5", kind = "recall_key", value = 5 },
    { input = "123", kind = "recall_key", value = 123 },
    { input = "3s", kind = "recall_time", value = 3 },
    { input = "90s", kind = "recall_time", value = 90 },
  }) do
    local t, v = alias.parse(case.input)
    assert_eq(t, case.kind, case.input)
    assert_eq(v, case.value, case.input)
  end
end)

test("parse: rejects invalid alias shapes", function()
  for _, input in ipairs({
    "0", "0s", "my-refactor", "_tmp",
    "tmp1", "v2", "1a", "3m", "3ms", "2h",
    "", " ", "a b", " a",
  }) do
    assert_eq(alias.parse(input), nil, input)
  end
  assert_eq(alias.parse(nil), nil)
  assert_eq(alias.parse(42), nil)
  assert_eq(alias.parse({}), nil)
  assert_eq(alias.parse(true), nil)
end)

test("classification helpers distinguish manual and recall aliases", function()
  for _, input in ipairs({ "a", "refactor", "FOO" }) do
    assert_eq(alias.is_valid_manual(input), true, input)
  end
  for _, input in ipairs({ "my-ref", "3", "3s", "", "tmp1" }) do
    assert_eq(alias.is_valid_manual(input), false, input)
  end
  assert_eq(alias.is_valid_manual(nil), false)

  for _, input in ipairs({ "3", "3s", "120" }) do
    assert_eq(alias.is_recall(input), true, input)
  end
  for _, input in ipairs({ "a", "3m", "" }) do
    assert_eq(alias.is_recall(input), false, input)
  end
  assert_eq(alias.is_recall(nil), false)
end)

test("TIME_HINTS all parse as recall_time", function()
  assert_true(#alias.TIME_HINTS > 0, "expected some hints")
  for _, h in ipairs(alias.TIME_HINTS) do
    local t = alias.parse(h)
    assert_eq(t, "recall_time", "hint " .. tostring(h) .. " should be recall_time")
  end
end)

test("is_valid_saved_name: accepts safe filename-like names", function()
  for _, input in ipairs({
    "refactor", "bugfix_v2", "refactor-2026-04-13", "my.edit.v2",
    "a", "_internal", "session42", "42session",
  }) do
    assert_eq(alias.is_valid_saved_name(input), true, input)
  end
end)

test("is_valid_saved_name: rejects unsafe names", function()
  for _, input in ipairs({
    "..", ".", "../foo", "../../etc/passwd", "foo/bar", "foo\\bar",
    "/etc/passwd", "/absolute", ".hidden", ".bashrc", "-rf", "-name",
    "name with space", "tab\there", "new\nline", "null\0byte",
    " leading", "trailing ", "", "name;rm -rf", "name$VAR",
    "name|pipe", "name*glob", "name:colon",
  }) do
    assert_eq(alias.is_valid_saved_name(input), false, input)
  end
  assert_eq(alias.is_valid_saved_name(nil), false)
  assert_eq(alias.is_valid_saved_name(42), false)
  assert_eq(alias.is_valid_saved_name({}), false)
  assert_eq(alias.is_valid_saved_name(true), false)
end)
