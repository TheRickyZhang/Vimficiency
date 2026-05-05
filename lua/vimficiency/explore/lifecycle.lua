local ffi_lib      = require("vimficiency.ffi")
local activation   = require("vimficiency.explore.activation")
local panel_render = require("vimficiency.explore.render.panel")

local M = {}
local v = vim.api

---@param view VF.Explore.Active
function M.destroy(view)
  local tab = view.scratch.tab

  panel_render.close(view)
  activation.detach(view)
  ffi_lib.explore_destroy(view.view_id)

  vim.schedule(function()
    if tab and v.nvim_tabpage_is_valid(tab) then
      pcall(function()
        v.nvim_set_current_tabpage(tab)
        vim.cmd("tabclose")
      end)
    end
  end)
end

return M
