local sequence_display = require("vimficiency.sequence_display")

test("sequence_display.lines tokenizes and sectionizes by default", function()
  local lines = sequence_display.lines("3wciwfoo<Esc>2j")
  assert_eq(lines, { "3w", "ciw foo <Esc>", "2j" })
end)

test("sequence_display.lines keeps raw sections when tokenize=false", function()
  local lines = sequence_display.lines("3wciwfoo<Esc>2j", { tokenize = false })
  assert_eq(lines, { "3w", "ciwfoo<Esc>", "2j" })
end)

test("sequence_display.lines flattens when sectionize=false", function()
  local lines = sequence_display.lines("3wciwfoo<Esc>2j", { sectionize = false })
  assert_eq(lines, { "3w ciw foo <Esc> 2j" })
end)

test("sequence_display.inline always returns a flat string", function()
  local line = sequence_display.inline("3wciwfoo<Esc>2j")
  assert_eq(line, "3w ciw foo <Esc> 2j")
end)

test("sequence_display.prefixed_lines indents continuation lines", function()
  local lines = sequence_display.prefixed_lines("User seq: ", "3wciwfoo<Esc>2j")
  assert_eq(lines, {
    "User seq: 3w",
    "          ciw foo <Esc>",
    "          2j",
  })
end)
