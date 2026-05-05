local v = vim.api

local M = {}

---@type table<integer, VF.Explore.Active>
local active_by_tab = {}

---@return VF.Explore.Active
function M.current()
  local view = active_by_tab[v.nvim_get_current_tabpage()]
  assert(view, "vimfy explore view not active in current tab")
  return view
end

---@return boolean
function M.current_buffer_is_scratch()
  local view = active_by_tab[v.nvim_get_current_tabpage()]
  return view ~= nil and v.nvim_get_current_buf() == view.scratch.buf
end

---@return boolean
function M.has_any()
  return next(active_by_tab) ~= nil
end

---@param view VF.Explore.Active
function M.set(view)
  active_by_tab[view.scratch.tab] = view
end

---@param view VF.Explore.Active
function M.clear(view)
  active_by_tab[view.scratch.tab] = nil
end

return M
