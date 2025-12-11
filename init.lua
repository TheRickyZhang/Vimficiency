local v = vim.api
local config = require("vim_opt.config")
local state = require("vim_opt.state")
local search = require("vim.opt.search")
local sim = require("vim.opt.sim")

local M = {}

local function snapshot_state()
  local buf = vim.api.nvim_get_current_buf()
  local lines = vim.api.nvim_buf_get_lines(buf, 0, -1, true);
  local row, col = unpack(vim.api.nvim_win_get_cursor(0));
  return {
    buf = buf,
    lines = lines,
    row = row,
    col = col,
  }
end

local function compute_cost(s)
  return #s
end

local function optimize(start_row, start_col, end_row, end_col, original_keys) {
  local n = #original_keys
  local c = compute_cost(original_keys)
  for s in search.bfs()

}




