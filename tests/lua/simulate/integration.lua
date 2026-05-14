-- This file lives at tests/lua/simulate/integration.lua, so the plugin
-- root is three levels up and `_helpers` lives in tests/lua/.
local script = debug.getinfo(1, "S").source:sub(2)
local this_dir = script:match("(.*/)")
local tests_root = this_dir .. "../"
local plugin_root = this_dir .. "../../.."
vim.opt.rtp:prepend(plugin_root)
package.path = tests_root .. "?.lua;" .. package.path

local helpers = require("_helpers")
local sim = require("vimficiency.simulate")
local highlights = require("vimficiency.highlights")
local session = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

-- Quiet the environment — mirrors `runner.lua`'s treatment so this test
-- fits the same output shape as the main batch. See the runner for
-- rationale.
local verbose = vim.env.VF_TEST_VERBOSE == "1"
if not verbose then
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.notify = function(...) end
end
vim.o.showmode = false

local passed = 0
local failed = 0
---@type { name: string, err: string }[]
local failed_list = {}

---@param actual any
---@param expected any
---@param msg string
local function assert_eq(actual, expected, msg)
  if not vim.deep_equal(actual, expected) then
    error(string.format("%s: expected %s, got %s",
      msg, vim.inspect(expected), vim.inspect(actual)), 2)
  end
end

---@param cond any
---@param msg string
local function assert_true(cond, msg)
  if not cond then error(msg, 2) end
end

---@param name string
local function pass(name)
  passed = passed + 1
  if verbose then
    io.stdout:write("[       OK ] integration :: " .. name .. "\n")
  end
end

---@param name string
---@param err any
local function fail(name, err)
  failed = failed + 1
  failed_list[#failed_list + 1] = { name = name, err = tostring(err) }
  io.stderr:write(string.format(
    "[  FAILED  ] integration :: %s\n    %s\n",
    name, tostring(err):gsub("\n", "\n    ")))
end

---@param idx integer
---@return VF.Replay.Window
local function get_window(idx)
  local entry = sim._debug_get_windows()[idx]
  assert_true(entry ~= nil, "missing replay window " .. idx)
  return entry
end

---@param idx integer
---@param step integer
---@return VF.Replay.Snapshot
local function get_snapshot(idx, step)
  local seq_states = sim._debug_get_states()[idx]
  assert_true(seq_states ~= nil, "missing replay states for window " .. idx)
  local snap = seq_states[step + 1]
  assert_true(snap ~= nil, string.format("missing replay snapshot %d for window %d", step, idx))
  return snap
end

---@param buf integer
---@return string[]
local function header_lines(buf)
  local marks = vim.api.nvim_buf_get_extmarks(buf, -1, 0, -1, { details = true })
  for _, mark in ipairs(marks) do
    local details = mark[4]
    if details.virt_lines then
      ---@type string[]
      local lines = {}
      for _, row in ipairs(details.virt_lines) do
        local parts = {}
        for _, chunk in ipairs(row) do
          parts[#parts + 1] = chunk[1]
        end
        lines[#lines + 1] = table.concat(parts)
      end
      return lines
    end
  end
  error("missing virtual header")
end

---@param buf integer
---@return table[][]
local function header_virt_lines(buf)
  local marks = vim.api.nvim_buf_get_extmarks(buf, -1, 0, -1, { details = true })
  for _, mark in ipairs(marks) do
    local details = mark[4]
    if details.virt_lines then
      return details.virt_lines
    end
  end
  error("missing virtual header")
end

---@param buf integer
---@param text string
---@param hl string
---@return boolean
local function header_has_chunk(buf, text, hl)
  for _, row in ipairs(header_virt_lines(buf)) do
    for _, chunk in ipairs(row) do
      if chunk[1] == text and chunk[2] == hl then
        return true
      end
    end
  end
  return false
end

---@param sequences string[]
---@param lines string[]
---@param fn fun()
---@param next fun(ok: boolean, err: any)
---@param opts { start_row: integer?, start_col: integer?, user_seq: string? }?   0-indexed; default (0, 0)
local function with_replay(sequences, lines, fn, next, opts)
  local prev_notify = vim.notify
  local prev_echo = vim.api.nvim_echo
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.notify = function(...) end
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.api.nvim_echo = function(...) end

  local start_row = (opts and opts.start_row) or 0
  local start_col = (opts and opts.start_col) or 0
  local user_seq = opts and opts.user_seq or nil

  helpers.new_buf({ "simulate source" })
  vim.api.nvim_win_set_cursor(0, { 1, 0 })
  helpers.feed("<Esc>")

  local suggestions = {}
  for _, seq in ipairs(sequences) do
    suggestions[#suggestions + 1] = { seq = seq }
  end
  -- Seed play settings so every pane in the test set is visible. Without
  -- this the tests would be clamped to the default window_count (2).
  local play = require("vimficiency.play")
  local expected_windows = #suggestions + (user_seq and 1 or 0)
  play.set_setting("window_count", math.min(4, math.max(1, expected_windows)))
  play.set_setting("include_user_sequence", user_seq ~= nil)
  sim.simulate_compare(lines, start_row, start_col,
    {
      user = user_seq and { seq = user_seq } or nil,
      suggestions = suggestions,
    })

  local tries = 0
  local function finish(ok, err)
    sim.cleanup_compare()
    vim.notify = prev_notify
    vim.api.nvim_echo = prev_echo
    next(ok, err)
  end

  local function wait_ready()
    tries = tries + 1
    local windows = sim._debug_get_windows()
    local states = sim._debug_get_states()
    if #windows == expected_windows and #states == expected_windows then
      local ok, err = pcall(fn)
      finish(ok, err)
    elseif tries > 300 then
      finish(false, "simulate_compare did not finish precompute")
    else
      vim.defer_fn(wait_ready, 10)
    end
  end

  wait_ready()
end

---@param name string
---@return table
local function load_vimficiency_file(name)
  local path = plugin_root .. "/data/VimficiencyFiles/" .. name .. ".json"
  local fh = assert(io.open(path, "r"))
  local text = fh:read("*a")
  fh:close()
  return vim.json.decode(text)
end

local case_context = {
  helpers = helpers,
  sim = sim,
  highlights = highlights,
  session = session,
  session_store = session_store,
  with_replay = with_replay,
  get_window = get_window,
  get_snapshot = get_snapshot,
  header_lines = header_lines,
  header_has_chunk = header_has_chunk,
  load_vimficiency_file = load_vimficiency_file,
  assert_eq = assert_eq,
  assert_true = assert_true,
}

local cases = {}
for _, module_name in ipairs({
  "simulate._integration_cases_tokenize",
  "simulate._integration_cases_replay",
  "simulate._integration_cases_header",
  "simulate._integration_cases_session_window",
}) do
  for _, case in ipairs(require(module_name)(case_context)) do
    cases[#cases + 1] = case
  end
end

local idx = 1

local function finish_all()
  local total = passed + failed
  io.stdout:write(string.format(
    "\n[==========] %d tests from 1 files ran (integration)\n", total))
  io.stdout:write(string.format("[  PASSED  ] %d/%d tests\n", passed, total))
  if failed > 0 then
    io.stderr:write(string.format("[  FAILED  ] %d/%d tests, listed below:\n",
      failed, total))
    for _, ft in ipairs(failed_list) do
      io.stderr:write(string.format("[  FAILED  ] integration :: %s\n", ft.name))
    end
    io.stderr:write(string.format("\n%d FAILED TESTS\n", failed))
  end
  -- Same machine-readable summary `run.sh` looks for on the runner side.
  -- Writes to the env-var'd summary file if present; no-op otherwise.
  if vim.env.VF_TEST_SUMMARY_FILE and vim.env.VF_TEST_SUMMARY_FILE ~= "" then
    local fh = io.open(vim.env.VF_TEST_SUMMARY_FILE, "a")
    if fh then
      fh:write(string.format("passed=%d failed=%d tests=%d\n", passed, failed, total))
      fh:close()
    end
  end
  vim.cmd((failed == 0) and "cquit 0" or "cquit 1")
end

local function run_next()
  if idx > #cases then
    finish_all()
    return
  end

  local case = cases[idx]
  idx = idx + 1
  case.run(function(ok, err)
    if ok then
      pass(case.name)
    else
      fail(case.name, err)
    end
    vim.schedule(run_next)
  end)
end

vim.schedule(run_next)
