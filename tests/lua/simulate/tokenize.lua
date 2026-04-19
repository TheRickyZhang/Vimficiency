local sim = require("vimficiency.simulate")

test("simulate tokenization merges feedable commands", function()
  assert_eq(table.concat(sim._debug_tokenize_for_animation("jf;i<BS>3<Esc><Space>ve"), "|"),
    "j|f;|i|<BS>|3|<Esc>|<Space>|v|e")
  assert_eq(table.concat(sim._debug_tokenize_for_animation("3wfa;ww"), "|"),
    "3w|fa;|w|w")
  assert_eq(table.concat(sim._debug_tokenize_for_animation("<Space>ww"), "|"),
    "<Space>|w|w")
  assert_eq(table.concat(sim._debug_tokenize_for_animation("Axyz<Esc>"), "|"),
    "A|xyz|<Esc>")
end)
