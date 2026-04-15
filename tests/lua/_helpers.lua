-- tests/lua/_helpers.lua
-- Shared fixtures for the Lua test suite. Filename is underscore-prefixed
-- so the runner's glob skips it.

local M = {}

--- Create a scratch buffer, fill it, make it current, return its id.
---@param lines string[]|nil  Defaults to { "x" }.
---@return integer buf
function M.new_buf(lines)
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines or { "x" })
  vim.api.nvim_set_current_buf(buf)
  return buf
end

--- Install one or more monkey-patches, run `fn`, then restore — even on
--- error. Each patch is a `{table, key, value}` triple. Errors surface
--- from `fn` after restore, with preserved traceback.
---@param patches table<integer, table>  Array of { tbl, key, value }.
---@param fn fun()
function M.with_patch(patches, fn)
  local saved = {}
  for i, p in ipairs(patches) do
    saved[i] = { tbl = p[1], key = p[2], prev = p[1][p[2]] }
    p[1][p[2]] = p[3]
  end
  local ok, err = pcall(fn)
  for i = #saved, 1, -1 do
    saved[i].tbl[saved[i].key] = saved[i].prev
  end
  if not ok then error(err, 2) end
end

--- Canonical result table used by auto_suggest / session_kinds tests.
--- Accepts overrides to customize per-test specifics (user_cost,
--- optimal_results, etc.).
---@param overrides table|nil
---@return table
function M.fake_result(overrides)
  local r = {
    start_row = 0, start_col = 0, end_row = 0, end_col = 0,
    user_seq = "x",
    optimal_results = { { seq = "y", cost = 1.0 } },
  }
  if overrides then
    for k, v in pairs(overrides) do r[k] = v end
  end
  return r
end

--- Run `fn` with `vim.notify` and `util.show_output` stubbed to no-op.
---@param fn fun()
function M.silence_notify(fn)
  local util = require("vimficiency.util")
  M.with_patch({
    { vim, "notify", function() end },
    { util, "show_output", function() end },
  }, fn)
end

--- Install a temporary keymap via `vimfy.map`, run `fn`, clean up the
--- mapping on exit. Accepts the same spec forms as `vimfy.map`.
---@param mode string
---@param lhs string
---@param spec string|function
---@param fn fun()
---@param opts table|nil  Forwarded to vimfy.map.
function M.with_temp_map(mode, lhs, spec, fn, opts)
  local vimfy = require("vimficiency")
  pcall(vim.keymap.del, mode, lhs)
  vimfy.map(mode, lhs, spec, opts)
  local ok, err = pcall(fn)
  pcall(vim.keymap.del, mode, lhs)
  if not ok then error(err, 2) end
end

--- Seed a recall record in the store. `backdate_s` backdates
--- `time_started` so `Ns` queries can resolve the record deterministically.
---@param opts { buf?: integer, lines?: string[], id?: string, backdate_s?: number }|nil
---@return table rec, integer buf, string id
function M.seed_recall(opts)
  opts = opts or {}
  local session_store = require("vimficiency.session_store")
  local util = require("vimficiency.util")
  local lines = opts.lines or { "x" }
  local buf = opts.buf or M.new_buf(lines)
  local win = vim.api.nvim_get_current_win()
  local id = opts.id or util.new_id(buf)
  local rec = session_store.new_active_session(
    id, -1, win, buf,
    { row = 0, col = 0, lines = lines },
    "auto", "manual"
  )
  rec.first_mode = "n"
  if opts.backdate_s then
    rec.time_started = vim.uv.hrtime() - opts.backdate_s * 1e9
  end
  session_store.store_recall(rec)
  return rec, buf, id
end

return M
