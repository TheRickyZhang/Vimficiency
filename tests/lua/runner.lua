-- Minimal Lua test runner.
-- Run with `nvim --headless -l tests/lua/runner.lua` or `./tests/lua/run.sh`.
--
-- Output style mirrors gtest --gtest_brief=1: passes are silent, failures
-- print full detail inline, per-file one-liner, gtest-style summary at end.
-- Set `VF_TEST_VERBOSE=1` to also print `[       OK ]` lines per test.

local verbose = vim.env.VF_TEST_VERBOSE == "1"

-- Silence plugin-emitted `vim.notify` and `:Vimfy list` printout during
-- tests — our session lifecycle routinely calls `vim.notify` on start /
-- finish / close, which the test would otherwise spam to stdout. Tests
-- that actually need to observe a notification call should install their
-- own local patch via `_helpers.silence_notify` or a custom spy; those
-- overrides compose fine on top of this no-op. Verbose mode keeps
-- notifications visible for debugging.
if not verbose then
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.notify = function() end
  -- `vim.api.nvim_echo` is used by some subcommands (e.g. `:Vimfy list`
  -- dumps session state); redirect to no-op as well.
  local original_echo = vim.api.nvim_echo
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.api.nvim_echo = function() end
  -- Re-expose the original so the rare test that really needs echo
  -- output can temporarily restore it.
  _G.__vf_test_original_echo = original_echo
end

-- Disable the mode indicator (`-- INSERT --`, `-- VISUAL --`) — some
-- tests feedkeys into modal states and the indicator spills into stdout.
vim.o.showmode = false

-- Running totals and deferred failure list for the final summary.
local total_passed = 0
local total_failed = 0
---@type { file: string, name: string, err: string }[]
local failed_list = {}

-- Per-file counters reset by the outer loop.
local current_file = "<unknown>"
local file_passed = 0
local file_failed = 0

---@diagnostic disable-next-line: lowercase-global
function test(name, fn)
  local ok, err = pcall(fn)
  if ok then
    total_passed = total_passed + 1
    file_passed = file_passed + 1
    if verbose then
      io.stdout:write("[       OK ] " .. current_file .. " :: " .. name .. "\n")
    end
  else
    total_failed = total_failed + 1
    file_failed = file_failed + 1
    failed_list[#failed_list + 1] = { file = current_file, name = name, err = tostring(err) }
    -- Failure detail inline — mirrors gtest's "detail next to the FAIL" habit
    -- so scrolling isn't required to see why something broke.
    io.stderr:write(string.format(
      "[  FAILED  ] %s :: %s\n    %s\n",
      current_file, name, tostring(err):gsub("\n", "\n    ")))
  end
end

---@diagnostic disable-next-line: lowercase-global
function assert_eq(actual, expected, msg)
  if not vim.deep_equal(actual, expected) then
    error(string.format("%s: expected %s, got %s",
      msg or "assert_eq",
      vim.inspect(expected), vim.inspect(actual)), 2)
  end
end

---@diagnostic disable-next-line: lowercase-global
function assert_true(cond, msg)
  if not cond then
    error(msg or "expected truthy value", 2)
  end
end

--- Assert that `tostring(actual)` contains `substr` as a plain substring.
---@diagnostic disable-next-line: lowercase-global
function assert_match(actual, substr, msg)
  local s = tostring(actual)
  if not s:find(substr, 1, true) then
    error(string.format("%s: expected %s to contain %s",
      msg or "assert_match",
      vim.inspect(s), vim.inspect(substr)), 2)
  end
end

--- Assert that `fn` errors.
---@diagnostic disable-next-line: lowercase-global
function assert_error(fn, pattern, msg)
  local ok, err = pcall(fn)
  if ok then
    error((msg or "assert_error") .. ": call succeeded", 2)
  end
  if pattern and not tostring(err):find(pattern, 1, true) then
    error(string.format("%s: error message %q did not contain %q",
      msg or "assert_error", tostring(err), pattern), 2)
  end
end

-- Point runtimepath at the plugin root and expose `tests/lua` on `package.path`.
local this_file = debug.getinfo(1, "S").source:sub(2)
local tests_dir = this_file:match("(.*/)")
local plugin_root = tests_dir .. "../.."
vim.opt.rtp:prepend(plugin_root)
package.path = tests_dir .. "?.lua;" .. package.path

local test_files = {}
if vim.env.VF_TEST_FILE and vim.env.VF_TEST_FILE ~= "" then
  test_files[1] = vim.fn.fnamemodify(vim.env.VF_TEST_FILE, ":p")
else
  -- Discover test files recursively, skipping this runner, `_` helpers,
  -- and files that `run.sh` dispatches to their own processes.
  local isolated = {
    ["simulate/integration.lua"] = true,
    ["capture/on_key_mapping_probe.lua"] = true,
  }
  local candidates = vim.fn.glob(tests_dir .. "**/*.lua", false, true)
  for _, path in ipairs(candidates) do
    local name = vim.fn.fnamemodify(path, ":t")
    local rel = path:sub(#tests_dir + 1)
    if name ~= "runner.lua" and name:sub(1, 1) ~= "_" and not isolated[rel] then
      test_files[#test_files + 1] = path
    end
  end
  table.sort(test_files)
end

-- Reset editor + plugin state between test files so a single Neovim
-- process can host the whole suite instead of spawning one per file. The
-- old per-process model traded ~150ms × N startup cost for "nothing leaks
-- between files"; this reset covers the specific things our plugin and
-- tests actually mutate, so we keep the isolation guarantee and pay the
-- startup cost exactly once.
--
-- If a test starts flaking after the suite gains a new stateful module,
-- look here first: something's mutating state this function doesn't know
-- about yet. Prefer extending this list over reverting to per-file
-- processes — the drift is easier to debug than the throughput is to
-- claw back.
local function reset_state()
  -- 1. Global `vim.on_key` listener: key_tracking.shutdown tears down
  --    the shared namespace and clears all named subscribers. Without
  --    this, subscribers accumulate (recall capture, end triggers, any
  --    test-installed ones) and fire during subsequent tests.
  local kt = package.loaded["vimficiency.capture.key_tracking"]
  if kt and type(kt.shutdown) == "function" then kt.shutdown() end

  -- 2. Plugin augroup: init.lua creates `Vimficiency`; re-creating with
  --    `clear = true` is the idempotent way to wipe its autocmds.
  pcall(vim.api.nvim_create_augroup, "Vimficiency", { clear = true })

  -- 3. User command `:Vimfy` may have been registered by init.setup().
  pcall(vim.api.nvim_del_user_command, "Vimfy")

  -- 4. Collapse to one tab, one window, one buffer. `simulate` tests
  --    spawn extra tabs; session tests spawn scratch buffers.
  pcall(vim.cmd, "silent! tabonly")
  pcall(vim.cmd, "silent! only")
  local cur = vim.api.nvim_get_current_buf()
  for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if b ~= cur and vim.api.nvim_buf_is_valid(b) then
      pcall(vim.api.nvim_buf_delete, b, { force = true })
    end
  end

  -- 5. Env vars tests override (workspace_storage.lua sets XDG_DATA_HOME
  --    to a tempdir). Clear so the next test inherits pristine defaults.
  vim.env.XDG_DATA_HOME = nil

  -- 6. Unload plugin modules so the next file's top-level `require()`
  --    calls re-execute and see a fresh module-local state (session
  --    records, recall ring, config, etc.). Tests capture module refs
  --    at file load via `local foo = require(...)` — `dofile` re-runs
  --    the file, so those captures re-fetch, but only if the cache is
  --    cleared first. `_helpers` and the test runner's own modules are
  --    deliberately left intact.
  for name in pairs(package.loaded) do
    if name == "vimficiency" or name:match("^vimficiency%.") then
      package.loaded[name] = nil
    end
  end
end

local t_start = vim.uv.hrtime()

for i, path in ipairs(test_files) do
  current_file = vim.fn.fnamemodify(path, ":t")
  file_passed, file_failed = 0, 0

  -- Reset BEFORE every file after the first — the first starts pristine
  -- from a just-booted Neovim, and post-file reset would mask bugs in the
  -- last file's teardown (everything after the last test is unchecked).
  if i > 1 then reset_state() end
  dofile(path)

  -- One-liner per file: suppress for silently-passing files when not in
  -- verbose mode — the green line per file was the main source of the
  -- "wall of text" complaint. Failing files still surface here.
  local total = file_passed + file_failed
  if file_failed > 0 then
    io.stdout:write(string.format(
      "[  FAILED  ] %s (%d failed / %d tests)\n", current_file, file_failed, total))
  elseif verbose then
    io.stdout:write(string.format(
      "[       OK ] %s (%d tests)\n", current_file, total))
  end
end

local elapsed_ms = (vim.uv.hrtime() - t_start) / 1e6
local total = total_passed + total_failed

io.stdout:write(string.format(
  "\n[==========] %d tests from %d files ran (%.0fms)\n",
  total, #test_files, elapsed_ms))
-- Always print `N/N` even on full pass so the count is unambiguously a
-- pass count, not a tests-ran count. Without the denominator, readers
-- who saw intermediate output can't tell at a glance whether "5 tests"
-- meant "5 passed" or "5 ran with unknown outcome".
io.stdout:write(string.format("[  PASSED  ] %d/%d tests\n", total_passed, total))
if total_failed > 0 then
  io.stderr:write(string.format("[  FAILED  ] %d/%d tests, listed below:\n",
    total_failed, total))
  for _, ft in ipairs(failed_list) do
    io.stderr:write(string.format("[  FAILED  ] %s :: %s\n", ft.file, ft.name))
  end
  io.stderr:write(string.format("\n%d FAILED TESTS\n", total_failed))
end

-- Machine-readable summary for `run.sh` to aggregate across processes.
-- Writes to `VF_TEST_SUMMARY_FILE` if set, otherwise no-op. Routing
-- through a file keeps the human stdout uncluttered; the shell picks up
-- the counts by reading the file after all phases have run.
if vim.env.VF_TEST_SUMMARY_FILE and vim.env.VF_TEST_SUMMARY_FILE ~= "" then
  local fh = io.open(vim.env.VF_TEST_SUMMARY_FILE, "a")
  if fh then
    fh:write(string.format("passed=%d failed=%d tests=%d\n",
      total_passed, total_failed, total))
    fh:close()
  end
end

vim.cmd((total_failed == 0) and "cquit 0" or "cquit 1")
