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

return M
