local v = vim.api
local util = require("vimficiency.util")

local M = {}

local SCRATCH_BUFFER_DEFAULTS = {
  buftype    = "nofile",
  bufhidden  = "wipe",
  swapfile   = false,
  modifiable = false,
}

local BUFFER_OPTIONS = {
  "shiftwidth",
  "tabstop",
  "softtabstop",
  "expandtab",
  "iskeyword",
  "matchpairs",
  "virtualedit",
  "filetype",
}

local WINDOW_OPTIONS = {
  "number",
  "relativenumber",
  "cursorline",
  "wrap",
}

function M.configure_scratch_buffer(buf, name, options)
  if name then v.nvim_buf_set_name(buf, name) end
  for k, value in pairs(SCRATCH_BUFFER_DEFAULTS) do
    vim.bo[buf][k] = value
  end
  for k, value in pairs(options) do
    vim.bo[buf][k] = value
  end
end

function M.create_scratch_buffer(name, options)
  local buf = v.nvim_create_buf(false, true)
  M.configure_scratch_buffer(buf, name, options)
  return buf
end

local function split_and_capture(cmd)
  local tab = v.nvim_get_current_tabpage()
  local before = {}
  for _, w in ipairs(v.nvim_tabpage_list_wins(tab)) do before[w] = true end
  vim.cmd(cmd)
  for _, w in ipairs(v.nvim_tabpage_list_wins(tab)) do
    if not before[w] then return w end
  end
  error("vimfy explore: split " .. cmd .. " did not create a new window")
end

---@param anchor_win integer
---@param side "left"|"right"
---@param name string
---@param buffer_options table
---@param window_options table?
---@return integer buf
---@return integer win
function M.create_side_pane(anchor_win, side, name, buffer_options, window_options)
  v.nvim_set_current_win(anchor_win)
  local split_cmd
  if side == "left" then
    split_cmd = "topleft vsplit"
  elseif side == "right" then
    split_cmd = "botright vsplit"
  else
    error("vimfy explore: unknown side pane side '" .. tostring(side) .. "'", 0)
  end

  local win = split_and_capture(split_cmd)
  local buf = M.create_scratch_buffer(name, buffer_options)
  v.nvim_win_set_buf(win, buf)
  util.configure_side_pane(win, window_options)
  return buf, win
end

function M.copy_buffer_options(src_buf, scratch_buf)
  for _, opt in ipairs(BUFFER_OPTIONS) do
    local ok, value = pcall(v.nvim_get_option_value, opt, { buf = src_buf })
    if ok then
      pcall(v.nvim_set_option_value, opt, value, { buf = scratch_buf })
    end
  end
end

function M.copy_window_options(src_win, scratch_win)
  for _, opt in ipairs(WINDOW_OPTIONS) do
    local ok, value = pcall(v.nvim_get_option_value, opt, { win = src_win })
    if ok then
      pcall(v.nvim_set_option_value, opt, value, { win = scratch_win })
    end
  end
end

return M
