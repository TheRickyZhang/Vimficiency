local sim = require("vimficiency.simulate")

-- `_debug_tokenize_for_animation` returns VimficiencyToken[] after the
-- metadata-across-FFI refactor. Tests over kinded output: assert both
-- the concatenated token texts (split shape) AND the per-token kinds
-- (the classifier-via-metadata guarantee).

---@param seq string
---@param expected_texts string   -- pipe-delimited, matches the concat pattern
---@param expected_kinds string   -- pipe-delimited, same length
local function assert_tokenization(seq, expected_texts, expected_kinds)
  local tokens = sim._debug_tokenize_for_animation(seq)
  local texts, kinds = {}, {}
  for _, t in ipairs(tokens) do
    texts[#texts + 1] = t.text
    kinds[#kinds + 1] = t.kind
  end
  assert_eq(table.concat(texts, "|"), expected_texts, "texts for seq " .. seq)
  assert_eq(table.concat(kinds, "|"), expected_kinds, "kinds for seq " .. seq)
end

test("simulate tokenization merges feedable commands", function()
  -- `i<BS>3<Esc>` drops into insert; `<BS>` and `3` are typed text chunks.
  -- `<Space>` and `w`/`e` are motions; `v` is visual-entering.
  assert_tokenization(
    "jf;i<BS>3<Esc><Space>ve",
    "j|f;|i|<BS>|3|<Esc>|<Space>|v|e",
    "movement|movement|change|typed|typed|escape|movement|visual|movement")

  assert_tokenization(
    "3wfa;ww",
    "3w|fa;|w|w",
    "movement|movement|movement|movement")

  assert_tokenization(
    "<Space>ww",
    "<Space>|w|w",
    "movement|movement|movement")

  -- `A` enters insert; chunks are typed; `<Esc>` exits insert.
  assert_tokenization(
    "Axyz<Esc>",
    "A|xyz|<Esc>",
    "change|typed|escape")
end)
