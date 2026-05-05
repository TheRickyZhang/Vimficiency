local M = {}

local v = vim.api
local on_key_ns = v.nvim_create_namespace("vimfy_explore_on_key")
local installed = false

---@param on_key fun(key:string|nil, typed:string|nil)
function M.install(on_key)
  if installed then return end
  vim.on_key(function(key, typed)
    on_key(key, typed)
  end, on_key_ns)
  installed = true
end

function M.uninstall()
  if not installed then return end
  vim.on_key(nil, on_key_ns)
  installed = false
end

return M
