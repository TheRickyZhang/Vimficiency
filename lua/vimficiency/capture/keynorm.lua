-- lua/vimficiency/capture/keynorm.lua
--
-- Shared "any key representation → C++ tokenizer-safe printable form"
-- conversion.
--
-- `vim.on_key` delivers keys as raw bytes (e.g. 0x15 for <C-u>). The C++
-- sequence tokenizer (SequenceToKeys / MotionToKeysPrimitives) keys on the
-- printable `<C-x>` form, and `vim.fn.keytrans()` emits `<C-U>` (uppercase
-- modifier-letter), while the registered tokens are `<C-u>` (lowercase).
-- Without this normalization the tokenizer hits
-- `assert(false && "Malformed key sequence")` at runtime on any sequence
-- containing a Ctrl-letter.
--
-- See dev/lua/key-normalization.md for the full rationale and contract.
--
-- Callers:
--   - lua/vimficiency/explore/init.lua — on_key buffer for the explore session
--   - lua/vimficiency/capture/key_tracking.lua — mark/watch/recall capture
--
-- Any new callsite that forwards on_key output to the optimizer/FFI MUST
-- route through `normalize` rather than calling `vim.fn.keytrans` directly,
-- otherwise it will reintroduce the assert on Ctrl-letter input.

local M = {}

--- Convert any key representation to the tokenizer's canonical printable
--- form. Idempotent: `normalize(normalize(x)) == normalize(x)` for every
--- input shape (raw bytes, printable notation, or a mix).
---
--- Implementation: parse through `nvim_replace_termcodes` first so the
--- input is always in raw-byte form before `keytrans()` runs. That kills
--- the `<` → `<lt>` escape trap of calling `keytrans` on already-printable
--- text — pre-existing `<lt>` collapses back to `<`, then `keytrans`
--- re-escapes it exactly once.
---@param input string|nil
---@return string
function M.normalize(input)
  if not input or input == "" then return "" end
  local raw = vim.api.nvim_replace_termcodes(input, true, true, true)
  local printable = vim.fn.keytrans(raw)
  return (printable:gsub("<C%-([A-Z])>", function(c)
    return "<C-" .. c:lower() .. ">"
  end))
end

return M
