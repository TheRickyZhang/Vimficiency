local v = vim.api

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
