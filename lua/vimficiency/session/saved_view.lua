local disk = require("vimficiency.session.disk")
local sequence_display = require("vimficiency.sequence_display")
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

  local user_cost_str = data.user_cost and string.format(" (cost: %.2f)", data.user_cost) or ""
  local output_lines = {
    "=== " .. name .. " ===",
    "",
    string.format("Position: (%d, %d) -> (%d, %d)",
      data.start_row, data.start_col,
      data.end_row, data.end_col),
    "",
  }
  if data.user_seq and data.user_seq ~= "" then
    vim.list_extend(output_lines,
      sequence_display.prefixed_lines("User sequence: ", data.user_seq, nil, user_cost_str))
  else
    table.insert(output_lines, "User sequence: (none)" .. user_cost_str)
  end
  table.insert(output_lines, "")
  table.insert(output_lines, "Optimal motions:")

  local optimal = data.optimal_results or {}
  for i, r in ipairs(optimal) do
    vim.list_extend(output_lines,
      sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq, nil,
        string.format(" (cost: %.2f)", r.cost or 0)))
  end

  if #optimal == 0 then
    table.insert(output_lines, "  (no results)")
  end

  table.insert(output_lines, "")
  table.insert(output_lines, "Buffer context: (start, end marked with < >)")
  local lines = data.lines or {}
  for i, line in ipairs(lines) do
    local prefix = "  "
    if i - 1 == data.start_row then
      prefix = "> "
    elseif i - 1 == data.end_row then
      prefix = "< "
    end
    table.insert(output_lines, prefix .. line)
  end

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
