local ffi_explore     = require("vimficiency.ffi.explore")
local highlights      = require("vimficiency.explore.highlights")
local lifecycle       = require("vimficiency.explore.lifecycle")
local panel_render    = require("vimficiency.explore.render.panel")
local registry        = require("vimficiency.explore.registry")
local result_mod      = require("vimficiency.explore.result")
local runtime         = require("vimficiency.explore.runtime")
local settings        = require("vimficiency.explore.settings")
local settings_schema = require("vimficiency.explore.settings_schema")
local state           = require("vimficiency.explore.state")
local view_model      = require("vimficiency.explore.view")
local handlers        = require("vimficiency.explore.handlers")

local M = {}

---@param view VF.Explore.Active
---@return boolean
local function apply_history(view, fn)
  handlers.clear_key_buffer(view)
  local result = fn(view.view_id)
  if result.status == "Rejected" then
    vim.notify("vimfy explore: " .. result.reason, vim.log.levels.INFO)
    return false
  end
  state.restore(view)
  return true
end

local function undo()
  return apply_history(registry.current(), ffi_explore.explore_undo)
end

local function redo()
  return apply_history(registry.current(), ffi_explore.explore_redo)
end

local function undo_all()
  local view = registry.current()
  handlers.clear_key_buffer(view)
  local steps = 0
  while true do
    local result = ffi_explore.explore_undo(view.view_id)
    if result.status == "Rejected" then break end
    steps = steps + 1
  end
  if steps == 0 then
    vim.notify("vimfy explore: nothing to undo", vim.log.levels.INFO)
    return false
  end
  state.restore(view)
  return true
end

local function toggle_settings_panel()
  local view = registry.current()
  handlers.clear_key_buffer(view)
  if panel_render.is_open(view) then
    panel_render.close(view)
    return
  end
  local refresh = function()
    if view.plan_reconfigure_pending then
      view.plan_reconfigure_pending = nil
      state.reconfigure_plan(view)
      return
    end
    state.refresh_ui(view)
  end
  panel_render.open(view, settings_schema.build(view, refresh), refresh)
end


---@return table<string, function>
local function header_handlers()
  return {
    cancel        = function() M.cancel() end,
    undo          = undo,
    redo          = redo,
    undo_all      = undo_all,
    open_settings = toggle_settings_panel,
    debug_dump    = function()
      vim.notify(vim.inspect(registry.current()), vim.log.levels.INFO)
    end,
  }
end

--- OPTIONAL PARAMS OR RETURNS are a CODE SMELL!!
--- BEGIN actual external API

---@param label string
---@param result VF.Explore.OpenResult
---@return boolean
function M.open(label, result)
  highlights.refresh()

  local valid, err = result_mod.validate(result)
  if not valid then
    vim.notify("vimfy explore failed: " .. err, vim.log.levels.ERROR)
    return false
  end
  ---@cast result VF.Explore.OpenResult

  local view = runtime.start(label, result, header_handlers())
  state.refresh_ui(view)
  vim.notify("vimfy explore opened [" .. label .. "]", vim.log.levels.INFO)
  return true
end

function M.cancel()
  local view = registry.current()
  lifecycle.destroy(view)
  vim.notify("vimfy explore cancelled", vim.log.levels.INFO)
  return true
end

function M.status()
  return view_model.status(registry.current())
end

--- END external API

M._for_test = {
  default_settings = settings.default_settings,
  resolve_param = settings.resolve_param,
}

return M
