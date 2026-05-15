return function(ctx)
  local sim = ctx.sim
  local assert_eq = ctx.assert_eq

  return {
  {
    name = "simulate tokenization emits replay-safe command steps",
    run = function(next)
      next(pcall(function()
        -- Tokens are `{text, kind}` records after the FFI metadata refactor;
        -- extract `.text` for the split-shape assertions below. Kind
        -- assertions live in the standalone `tokenize.lua` test.
        local function texts(seq)
          local out = {}
          for _, t in ipairs(sim._debug_tokenize_for_animation(seq)) do
            out[#out + 1] = t.text
          end
          return table.concat(out, "|")
        end
        assert_eq(texts("jf;i<BS>3<Esc><Space>ve"),
          "j|f;|i|<BS>|3|<Esc>|<Space>|v|e", "tokenized insert/visual sequence")
        assert_eq(texts("3wfa;ww"),
          "3w|fa|;|w|w", "tokenized motion sequence")
        assert_eq(texts("f;;"),
          "f;|;", "tokenized semicolon target and repeat")
        assert_eq(texts("<Space>ww"),
          "<Space>|w|w", "tokenized leading space")
        assert_eq(texts("Axyz<Esc>"),
          "A|xyz|<Esc>", "tokenized append-at-eol")
        assert_eq(texts("A2<Space>*<Space>i<Esc>"),
          "A|2|<Space>|*|<Space>|i|<Esc>", "tokenized insert-mode spaces")
        assert_eq(texts("Afoo<CR>bar<Esc>"),
          "A|foo|<CR>|bar|<Esc>", "tokenized insert-mode CR")
      end))
    end,
  },
  }
end
