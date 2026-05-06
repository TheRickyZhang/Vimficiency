local v                = vim.api
local config           = require("vimficiency.config")
local ffi_lib          = require("vimficiency.ffi")
local header_render    = require("vimficiency.explore.render.header")
local list_render      = require("vimficiency.explore.render.list")
local tags_render      = require("vimficiency.explore.render.tags")
local settings         = require("vimficiency.explore.settings")
local insert_helpers   = require("vimficiency.explore.insert_helpers")

local M = {}

---@return string
function M.resolve_overrides()
  local store = settings.settings_store()
  local shared = vim.tbl_extend("force", {}, config.optimizer, store.shared)
  return ffi_lib.encode_optimizer_overrides({
    shared = shared,
    nav = store.nav,
    transform = store.transform,
    composition = store.composition,
  })
end

---@param a VF.Explore.Active
function M.fetch(a)
  a.state = ffi_lib.explore_state(a.view_id)
  a.recommendations = ffi_lib.explore_recommendations(
    a.view_id, a.recommendation_count, M.resolve_overrides(),
    a.recommendation_sort)
  a.header_rows = ffi_lib.explore_header_rows(a.view_id)
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
function M.reload_buffer(a)
  local lines = ffi_lib.explore_current_lines(a.view_id)
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
