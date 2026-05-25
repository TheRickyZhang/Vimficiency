local disk = require("vimficiency.session.disk")
local result_view = require("vimficiency.session.result_view")
local util = require("vimficiency.util")

local M = {}

---@param name string
function M.open(name)
  if not name or name == "" then
    local saved = disk.list()
    if #saved == 0 then
      vim.notify("No saved results found", vim.log.levels.INFO)
    else
      vim.notify("Saved results:\n  " .. table.concat(saved, "\n  "), vim.log.levels.INFO)
    end
    return
  end

  local data, err = disk.load(name)
  if not data then
    vim.notify("Failed to load '" .. name .. "': " .. (err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  local output_lines = result_view.format_saved_lines(name, data)

  vim.cmd("botright new")
  local buf = vim.api.nvim_get_current_buf()
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency"
  vim.api.nvim_buf_set_name(buf, "vimficiency://" .. name)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, output_lines)
  vim.bo[buf].modifiable = false

  util.set_buffer_keymaps(buf, util.with_standard_ui_keymaps({
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close vimfy view", nowait = true },
  }, {
    title = "Vimfy Saved Result Keys",
    docs = true,
  }))
end

return M
