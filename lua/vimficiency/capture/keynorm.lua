-- lua/vimficiency/capture/keynorm.lua
--
-- Convert raw input bytes from `vim.on_key` into the C++ tokenizer's
-- canonical printable form.
--
-- `vim.on_key` is nvim's lowest-level key hook: it delivers the bytes
-- nvim itself read from the input stream. Concretely:
--   - Plain ASCII / typed UTF-8: literal bytes (`a`, `é`).
--   - Ctrl-letters: a single control byte (`\x15` for `<C-u>`).
--   - Special keys (BS, arrows, Home/End, F-keys, …): K_SPECIAL form
--     (`\x80kb` for `<BS>`, `\x80ku` for arrow up, etc.).
-- It never delivers printable `<C-u>`-style notation — that's a vim
-- script surface form, not an input-stream form.
--
-- The C++ sequence tokenizer (SequenceToKeys / MovementToKeysPrimitives)
-- keys on the printable form with **lowercase** modifier letters
-- (`<C-u>`, not the `<C-U>` `keytrans` emits). So this helper is
-- exactly two passes: `keytrans` to print, then a lowercase-fixup to
-- match the project's token convention.
--
-- Callers:
--   - lua/vimficiency/explore.lua — on_key buffer for the explore session
--   - lua/vimficiency/capture/key_tracking.lua — mark/watch/recall capture
--
-- See dev/lua/key-normalization.md for the full rationale.

local M = {}

--- Convert raw input bytes (as delivered by `vim.on_key`) to the C++
--- tokenizer's printable form with lowercase modifier letters. Input
--- MUST be raw bytes — passing already-printable notation is a bug
--- (it will be re-escaped: `keytrans("<BS>")` returns `<lt>BS>`).
---
--- Why no `nvim_replace_termcodes` pre-pass: `replace_termcodes` is
--- the inverse direction (printable → raw) and it actively corrupts
--- raw K_SPECIAL bytes by escaping `0x80` to `<80>`-notation, which
--- breaks every special key. `keytrans` handles all raw-byte shapes
--- directly: single-byte control codes, K_SPECIAL multi-byte keys,
--- and plain ASCII/UTF-8 alike.
---@param input string|nil  raw bytes from `vim.on_key`
---@return string           printable form, e.g. `<C-u>`, `<BS>`, `a`
function M.normalize(input)
  if not input or input == "" then return "" end
  return (vim.fn.keytrans(input):gsub("<C%-([A-Z])>", function(c)
    return "<C-" .. c:lower() .. ">"
  end))
end

return M
