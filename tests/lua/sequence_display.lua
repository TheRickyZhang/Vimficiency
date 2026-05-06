local sequence_display = require("vimficiency.sequence_display")

local function flatten_chunk_lines(lines)
  local out = {}
  for _, line in ipairs(lines) do
    local parts = {}
    for _, chunk in ipairs(line) do
      parts[#parts + 1] = chunk[1]
    end
    out[#out + 1] = table.concat(parts)
  end
  return out
end

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

test("sequence_display pretty-prints key-notation typed text", function()
  local lines = sequence_display.lines("cW2<Space>*<Space>i<Esc>")
  assert_eq(lines, { "cW 2␣*␣i <Esc>" })
end)

test("sequence_display groups contiguous edit tokens into one row", function()
  assert_eq(
    sequence_display.lines("jF+Xce2 * i<Esc>"),
    { "j F+", "X ce 2␣*␣i <Esc>" })
  assert_eq(
    sequence_display.lines("x.ciw2 * i<Esc>"),
    { "x . ciw 2␣*␣i <Esc>" })
end)

test("sequence_display pretty-prints key notation in command tokens", function()
  assert_eq(sequence_display.inline("f<Space>l"), "f␣ l")
  assert_eq(sequence_display.inline("f<lt>l"), "f< l")
  assert_eq(sequence_display.inline("r<Tab>"), "r⇥")
end)

test("sequence_display renders literal and key-notation spaces equivalently in typed text", function()
  assert_eq(
    sequence_display.lines("ce2 * i<Esc>"),
    { "ce 2␣*␣i <Esc>" })
  assert_eq(
    sequence_display.lines("ce2<Space>*<Space>i<Esc>"),
    { "ce 2␣*␣i <Esc>" })
end)

test("sequence_display decodes escaped angle brackets in typed text", function()
  assert_eq(
    sequence_display.lines("ciw<lt>Space><Esc>"),
    { "ciw <Space> <Esc>" })
  assert_eq(
    sequence_display.typed_text_inline("<lt>Esc>"),
    "<Esc>")
end)

test("sequence_display pretty-prints standalone typed text", function()
  local line = sequence_display.typed_text_inline("2<Space>*<Space>i")
  assert_eq(line, "2␣*␣i")
end)

test("sequence_display pretty-prints literal typed text without key notation", function()
  assert_eq(sequence_display.literal_typed_text_inline("2 * i"), "2␣*␣i")
  assert_eq(sequence_display.literal_typed_text_inline("<Space>"), "<Space>")
end)

test("sequence_display pretty-prints mixed insert continuation chunks", function()
  assert_eq(sequence_display.typed_chunks_inline({
    { kind = "key", text = "<BS>" },
    { kind = "literal", text = " * i" },
  }), "<BS> ␣*␣i")
end)

test("sequence_display chunked replay keeps typed chunks contiguous", function()
  local lines = sequence_display.chunked_lines_for_tokens({
    { text = "cW", kind = "change" },
    { text = "2", kind = "typed" },
    { text = "<Space>", kind = "typed" },
    { text = "*", kind = "typed" },
    { text = "<Space>", kind = "typed" },
    { text = "i", kind = "typed" },
    { text = "<Esc>", kind = "escape" },
  })
  assert_eq(flatten_chunk_lines(lines), { "cW 2␣*␣i <Esc>" })
end)

test("sequence_display pretty-prints tab and newline glyphs", function()
  assert_eq(
    sequence_display.typed_text_inline("<Tab>a<CR>b"),
    "⇥a↵b")
  assert_eq(
    sequence_display.typed_text_inline("\ta\nb"),
    "⇥a↵b")
end)

test("sequence_display.prefixed_lines indents continuation lines", function()
  local lines = sequence_display.prefixed_lines("User seq: ", "3wciwfoo<Esc>2j")
  assert_eq(lines, {
    "User seq: 3w",
    "          ciw foo <Esc>",
    "          2j",
  })
end)
