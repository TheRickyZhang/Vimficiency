local v = vim.api

local M = {}

---@class VF.Explore.AutocmdHandlers
---@field on_cursor_moved fun()
---@field on_buffer_changed fun()
---@field on_insert_enter fun()
---@field on_insert_text_changed fun()
---@field on_winclosed fun(event: table)

---Register the five autocmds the explore view depends on for the given
---scratch buffer. Returns the WinClosed autocmd id; the rest are
---buffer-scoped and die with the buffer.
---@param scratch_buf integer
---@param handlers VF.Explore.AutocmdHandlers
---@return integer winclosed_autocmd_id
function M.install(scratch_buf, handlers)
  v.nvim_create_autocmd("CursorMoved", {
    buffer = scratch_buf,
    callback = handlers.on_cursor_moved,
    desc = "vimfy explore: forward cursor move to view",
  })
  -- TextChangedI is deliberately NOT here: buffer state mid-typing isn't a valid target.
  v.nvim_create_autocmd({ "TextChanged", "InsertLeave" }, {
    buffer = scratch_buf,
    callback = handlers.on_buffer_changed,
    desc = "vimfy explore: validate buffer state vs. planned fencepost",
  })
  v.nvim_create_autocmd("InsertEnter", {
    buffer = scratch_buf,
    callback = handlers.on_insert_enter,
    desc = "vimfy explore: begin insert phase",
  })
  v.nvim_create_autocmd("TextChangedI", {
    buffer = scratch_buf,
    callback = handlers.on_insert_text_changed,
    desc = "vimfy explore: live-refresh insert remaining",
  })
  -- Pattern-based (not buffer-scoped) so it survives the scratch buffer being wiped.
  return v.nvim_create_autocmd("WinClosed", {
    pattern = "*",
    callback = handlers.on_winclosed,
    desc = "vimfy explore: tear down view when a primary pane closes",
  })
end

return M
