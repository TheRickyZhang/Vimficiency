local v                = vim.api
local config           = require("vimficiency.config")
local ffi_explore      = require("vimficiency.ffi.explore")
local ffi_optimizer    = require("vimficiency.ffi.optimizer")
local header_render    = require("vimficiency.explore.render.header")
local list_render      = require("vimficiency.explore.render.list")
local tags_render      = require("vimficiency.explore.render.tags")
local settings         = require("vimficiency.explore.settings")
local insert_helpers   = require("vimficiency.explore.insert_helpers")
local timing           = require("vimficiency.explore.timing")

local M = {}

---@return string
function M.resolve_overrides()
  local store = settings.settings_store()
  local shared = vim.tbl_extend("force", {}, config.optimizer, store.shared)
  return ffi_optimizer.encode_optimizer_overrides({
    shared = shared,
    nav = store.nav,
    transform = store.transform,
    composition = store.composition,
  })
end

---@param a VF.Explore.Active
function M.fetch(a)
  a.state = ffi_explore.explore_state(a.view_id)
  local t0 = vim.uv.hrtime()
  a.recommendations = ffi_explore.explore_recommendations(
    a.view_id, a.recommendation_count, M.resolve_overrides(),
    a.recommendation_sort)
  a.last_recommend_ms = (vim.uv.hrtime() - t0) / 1e6
  timing.note("recommendations", a.last_recommend_ms)
  a.header_rows = ffi_explore.explore_header_rows(a.view_id)
  for i, rec in ipairs(a.recommendations) do rec.rank = i end
end

---@param a VF.Explore.Active
function M.refresh_ui(a)
  M.fetch(a)
  v.nvim_win_set_cursor(a.scratch.win, { a.state.cursor.row + 1, a.state.cursor.col })
  local continuation = insert_helpers.current_continuation(a)
  header_render.render(a, continuation)
  list_render.render(a, continuation)
  tags_render.render(a)
end

---@param a VF.Explore.Active
---@return VF.Explore.ReconfigurePlanResult
function M.reconfigure_plan(a)
  local result = ffi_explore.explore_reconfigure_plan(a.view_id, M.resolve_overrides())
  if result.reset then
    a.pending = nil
    a.warning = nil
    a.on_key_buffer = ""
    a.on_key_events = {}
    M.reload_buffer(a)
    vim.notify("vimfy explore: optimizer settings changed the plan; reset explore state",
      vim.log.levels.INFO)
  end
  M.refresh_ui(a)
  return result
end

---@param a VF.Explore.Active
function M.reload_buffer(a)
  local lines = ffi_explore.explore_current_lines(a.view_id)
  v.nvim_buf_set_lines(a.scratch.buf, 0, -1, false, lines)
  vim.bo[a.scratch.buf].modified = false
end

---Restore backend buffer/cursor while suppressing snapshot autocmd re-entry.
---@param a VF.Explore.Active
function M.restore(a)
  if a.restoring == nil then a.restoring = true end
  M.reload_buffer(a)
  M.refresh_ui(a)
  a.restoring = nil
end

return M
