-- Convert raw input bytes from `vim.on_key` into the C++ tokenizer's
-- printable form. See dev/lua/key-normalization.md for the raw-byte contract.

local M = {}

--- Input MUST be raw bytes from `vim.on_key`, not printable notation.
---@param input string|nil  raw bytes from `vim.on_key`
---@return string           printable form, e.g. `<C-u>`, `<BS>`, `a`
function M.normalize(input)
  if not input or input == "" then return "" end
  return (vim.fn.keytrans(input):gsub("<C%-([A-Z])>", function(c)
    return "<C-" .. c:lower() .. ">"
  end))
end

--- True for any mouse event token (clicks, drags, releases, wheel, move).
--- These reach `vim.on_key` as `<...>` names that the C++ tokenizer does not
--- recognize, so they would otherwise be costed character-by-character.
---@param normalized string  output of `M.normalize`
---@return boolean
function M.is_mouse(normalized)
  if not normalized or normalized == "" then return false end
  return normalized:find("Mouse") ~= nil
    or normalized:find("ScrollWheel") ~= nil
    or normalized:find("Drag") ~= nil
    or normalized:find("Release") ~= nil
end

return M
