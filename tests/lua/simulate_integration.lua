local script = debug.getinfo(1, "S").source:sub(2)
local tests_dir = script:match("(.*/)")
local plugin_root = tests_dir .. "../.."
vim.opt.rtp:prepend(plugin_root)
package.path = tests_dir .. "?.lua;" .. package.path

local helpers = require("_helpers")
local sim = require("vimficiency.simulate")

local passed = 0
local failed = 0

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
  io.stdout:write("PASS  " .. name .. "\n")
end

---@param name string
---@param err any
local function fail(name, err)
  failed = failed + 1
  io.stderr:write("FAIL  " .. name .. "\n  " .. tostring(err) .. "\n")
end

---@param idx integer
---@return VimficiencyReplayWin
local function get_window(idx)
  local entry = sim._debug_get_windows()[idx]
  assert_true(entry ~= nil, "missing replay window " .. idx)
  return entry
end

---@param idx integer
---@param step integer
---@return ReplaySnapshot
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

---@param sequences string[]
---@param lines string[]
---@param fn fun()
---@param next fun(ok: boolean, err: any)
local function with_replay(sequences, lines, fn, next)
  local prev_notify = vim.notify
  local prev_echo = vim.api.nvim_echo
  vim.notify = function() end
  vim.api.nvim_echo = function() end

  helpers.new_buf({ "simulate source" })
  vim.api.nvim_win_set_cursor(0, { 1, 0 })
  vim.api.nvim_feedkeys(
    vim.api.nvim_replace_termcodes("<Esc>", true, false, true), "xt", false)

  sim.simulate_compare(lines, 0, 0, sequences)

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
    if #windows == #sequences and #states == #sequences then
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

---@type { name: string, run: fun(next: fun(ok: boolean, err: any)) }[]
local cases = {
  {
    name = "simulate tokenization merges feedable commands",
    run = function(next)
      next(pcall(function()
        assert_eq(table.concat(sim._debug_tokenize_for_animation("jf;i<BS>3<Esc><Space>ve"), "|"),
          "j|f;|i|<BS>|3|<Esc>|<Space>|v|e", "tokenized insert/visual sequence")
        assert_eq(table.concat(sim._debug_tokenize_for_animation("3wfa;ww"), "|"),
          "3w|fa;|w|w", "tokenized motion sequence")
        assert_eq(table.concat(sim._debug_tokenize_for_animation("<Space>ww"), "|"),
          "<Space>|w|w", "tokenized leading space")
        assert_eq(table.concat(sim._debug_tokenize_for_animation("Axyz<Esc>"), "|"),
          "A|xyz|<Esc>", "tokenized append-at-eol")
      end))
    end,
  },
  {
    name = "simulate replays insert and visual sequence with virtual header",
    run = function(next)
      with_replay({ "jf;i<BS>3<Esc><Space>ve", "j" }, {
        "one two",
        "abc;def ghi",
        "last line",
      }, function()
        local seq1 = get_window(1)
        assert_eq(get_snapshot(1, 3).cursor, { 2, 3 }, "snapshot cursor after entering insert")
        assert_eq(get_snapshot(1, 3).mode, "i", "snapshot mode after entering insert")

        sim._debug_seek_to(3)
        assert_eq(header_lines(seq1.buf)[2], "Mode INSERT", "insert header")

        assert_eq(get_snapshot(1, 4).lines,
          { "one two", "ab;def ghi", "last line" }, "snapshot lines after <BS>")
        sim._debug_seek_to(4)
        assert_eq(vim.api.nvim_buf_get_lines(seq1.buf, 0, -1, true),
          { "one two", "ab;def ghi", "last line" }, "rendered lines after <BS>")

        assert_eq(get_snapshot(1, 5).lines,
          { "one two", "ab3;def ghi", "last line" }, "snapshot lines after typed 3")
        sim._debug_seek_to(5)
        assert_eq(vim.api.nvim_buf_get_lines(seq1.buf, 0, -1, true),
          { "one two", "ab3;def ghi", "last line" }, "rendered lines after typed 3")

        assert_eq(get_snapshot(1, 8).mode, "v", "snapshot mode after visual enter")
        sim._debug_seek_to(8)
        assert_eq(header_lines(seq1.buf)[2], "Mode VISUAL", "visual header")
        assert_eq(get_snapshot(1, 9).cursor, { 2, 6 }, "snapshot cursor after visual e")
      end, next)
    end,
  },
  {
    name = "simulate handles leading space and append-at-eol",
    run = function(next)
      with_replay({ "3wfa;ww", "<Space>ww", "Axyz<Esc>" }, {
        "alpha beta; gamma",
        "delta epsilon",
        "zeta",
      }, function()
        local seq3 = get_window(3)
        assert_eq(get_snapshot(2, 3).cursor, { 1, 10 }, "snapshot cursor after leading-space replay")
        assert_eq(get_snapshot(3, 3).cursor, { 1, 19 }, "snapshot append-at-eol cursor")
        sim._debug_seek_to(3)
        assert_eq(vim.api.nvim_buf_get_lines(seq3.buf, 0, -1, true),
          { "alpha beta; gammaxyz", "delta epsilon", "zeta" }, "rendered append-at-eol lines")
      end, next)
    end,
  },
  {
    name = "simulate virtual header wraps long sequences",
    run = function(next)
      with_replay({ string.rep("w", 120), "j" }, {
        "alpha beta gamma delta epsilon zeta",
      }, function()
        local seq1 = get_window(1)
        local lines = header_lines(seq1.buf)
        assert_true(#lines > 3, "expected wrapped sequence lines")
        assert_eq(lines[1], "[1] Progress 0/120  Local 0/120", "progress line")
        assert_eq(lines[2], "Mode NORMAL", "mode line")
        assert_true(lines[3]:sub(1, 8) == "Sequence", "sequence line prefix")
      end, next)
    end,
  },
}

local idx = 1

local function finish_all()
  io.stdout:write(string.format("\n%d passed, %d failed\n", passed, failed))
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
