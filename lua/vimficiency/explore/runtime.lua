local handlers   = require("vimficiency.explore.handlers")
local activation = require("vimficiency.explore.activation")
local layout_mod = require("vimficiency.explore.layout")
local result_mod = require("vimficiency.explore.result")
local view_model = require("vimficiency.explore.view")

local M = {}
local v = vim.api

---@param label string
---@param result VF.Explore.OpenResult
---@param header_handlers table<string, function>
---@return VF.Explore.Active
function M.start(label, result, header_handlers)
  local source_buf = v.nvim_get_current_buf()
  local source_win = v.nvim_get_current_win()

  local view_id = result_mod.start_view(result, source_win)
  local layout = layout_mod.build(label, result.lines, source_buf, source_win)
  local view = view_model.create(label, result, view_id, layout, header_handlers)

  activation.attach(view, layout, handlers.capture_key, handlers.install)
  return view
end

return M
