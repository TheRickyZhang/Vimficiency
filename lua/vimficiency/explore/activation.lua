local key_capture = require("vimficiency.explore.key_capture")
local keymaps     = require("vimficiency.explore.keymaps")
local registry    = require("vimficiency.explore.registry")

local M = {}
local v = vim.api

---@alias VF.Explore.AutocmdInstaller fun(view: VF.Explore.Active, layout: VF.Explore.Layout): integer

---@param view VF.Explore.Active
---@param layout VF.Explore.Layout
---@param capture_key fun(key: string|nil, typed: string|nil)
---@param install_autocmds VF.Explore.AutocmdInstaller
function M.attach(view, layout, capture_key, install_autocmds)
  registry.set(view)
  key_capture.install(capture_key)
  keymaps.install(layout.scratch_buf, layout.list_buf, view.header_handlers)
  view.winclosed_autocmd = install_autocmds(view, layout)
end

---@param view VF.Explore.Active
function M.detach(view)
  if view.winclosed_autocmd then
    pcall(v.nvim_del_autocmd, view.winclosed_autocmd)
    view.winclosed_autocmd = nil
  end

  registry.clear(view)
  if not registry.has_any() then key_capture.uninstall() end
end

return M
