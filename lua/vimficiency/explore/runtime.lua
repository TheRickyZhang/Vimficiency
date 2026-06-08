local handlers   = require("vimficiency.explore.handlers")
local activation = require("vimficiency.explore.activation")
local ffi_explore = require("vimficiency.ffi.explore")
local layout_mod = require("vimficiency.explore.layout")
local lifecycle  = require("vimficiency.explore.lifecycle")
local poller     = require("vimficiency.async.poller")
local result_mod = require("vimficiency.explore.result")
local settings   = require("vimficiency.explore.settings")
local state_mod  = require("vimficiency.explore.state")
local timing     = require("vimficiency.explore.timing")
local view_model = require("vimficiency.explore.view")

local M = {}
local v = vim.api

local function render_computing(list_buf)
  vim.bo[list_buf].modifiable = true
  v.nvim_buf_set_lines(list_buf, 0, -1, false, { "", "  Computing optimal sequences…", "" })
  vim.bo[list_buf].modifiable = false
end

-- During the compute phase the cursor sits in the cloned scratch buffer but the
-- interactive handlers aren't installed yet (they need the plan). This minimal
-- WinClosed teardown is the only thing wired up, so closing the tab mid-compute
-- cancels the worker and cleans up. Replaced by the full set in activation.attach.
local function install_pending_teardown(view, layout)
  return v.nvim_create_autocmd("WinClosed", {
    pattern = "*",
    callback = function(args)
      local win = tonumber(args.match)
      if win == layout.scratch_win or win == layout.list_win then
        lifecycle.destroy(view)
      end
    end,
    desc = "vimfy explore: tear down pending view if closed before ready",
  })
end

---@param label string
---@param result VF.Explore.OpenResult
---@param header_handlers table<string, function>
---@return VF.Explore.Active
function M.start(label, result, header_handlers)
  local source_buf = v.nvim_get_current_buf()
  local source_win = v.nvim_get_current_win()

  local store = settings.settings_store()
  local job_id = result_mod.start_view_async(
    result, source_win, state_mod.resolve_overrides(), store.view.compute_deadline_ms)

  local layout = layout_mod.build(label, result.lines, source_buf, source_win)
  local view = view_model.create(label, result, nil, layout, header_handlers)

  render_computing(layout.list_buf)
  require("vimficiency.explore.registry").set(view)
  view.winclosed_autocmd = install_pending_teardown(view, layout)

  local t0 = vim.uv.hrtime()
  view.poll_handle = poller.start(
    function() return ffi_explore.explore_poll(job_id) end,
    function() ffi_explore.explore_cancel(job_id) end,
    function(view_id)
      timing.note("composition search", (vim.uv.hrtime() - t0) / 1e6)
      view.view_id = view_id
      if view.winclosed_autocmd then
        pcall(v.nvim_del_autocmd, view.winclosed_autocmd)
        view.winclosed_autocmd = nil
      end
      activation.attach(view, layout, handlers.capture_key, handlers.install)
      state_mod.refresh_ui(view)
    end)

  return view
end

return M
