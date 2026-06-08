local ffi_explore  = require("vimficiency.ffi.explore")
local activation   = require("vimficiency.explore.activation")
local panel_render = require("vimficiency.explore.render.panel")
local poller       = require("vimficiency.async.poller")

local M = {}
local v = vim.api

---@param view VF.Explore.Active
function M.destroy(view)
  local tab = view.scratch.tab

  -- May still be in the compute phase: cancel the worker and skip the backend
  -- view teardown (no view exists until the plan is ready).
  if view.poll_handle then poller.cancel(view.poll_handle) end

  panel_render.close(view)
  activation.detach(view)
  if view.view_id then ffi_explore.explore_destroy(view.view_id) end

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
