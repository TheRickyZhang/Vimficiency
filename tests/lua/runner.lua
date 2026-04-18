-- tests/lua/runner.lua
-- Minimal test runner for vimficiency Lua modules.
--
-- Invocation: nvim --headless -l tests/lua/runner.lua
--   or equivalently: ./tests/lua/run.sh
--
-- Each *.lua file in this directory (other than `runner.lua` itself and
-- files whose name begins with `_`) is loaded in sequence. Test files
-- register cases via the globally-exposed `test(name, fn)` and use
-- `assert_eq` / `assert_true` / `assert_match` / `assert_error` (all
-- globals) for checks. Exit code is zero only if every test passed.
--
-- Intentionally tiny. When the scope grows past "pure-Lua module tests,"
-- reach for plenary.nvim + busted. Until then, this keeps the repo
-- self-contained.

local passed = 0
local failed = 0

---@diagnostic disable-next-line: lowercase-global
function test(name, fn)
  local ok, err = pcall(fn)
  if ok then
    passed = passed + 1
    io.stdout:write("PASS  " .. name .. "\n")
  else
    failed = failed + 1
    io.stderr:write("FAIL  " .. name .. "\n  " .. tostring(err) .. "\n")
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

--- Assert that `tostring(actual)` contains `substr` as a plain (non-regex)
--- substring. Intended for error-message inspection:
---   assert_match(err, "not a table")
---@diagnostic disable-next-line: lowercase-global
function assert_match(actual, substr, msg)
  local s = tostring(actual)
  if not s:find(substr, 1, true) then
    error(string.format("%s: expected %s to contain %s",
      msg or "assert_match",
      vim.inspect(s), vim.inspect(substr)), 2)
  end
end

--- Assert that `fn` errors. If `pattern` is given, the error string must
--- contain it as a plain substring.
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

-- Locate this file, point runtimepath at the plugin root so `require`
-- finds lua/vimficiency/*, and put tests/lua/ on package.path so test
-- files can `require("_helpers")`.
local this_file = debug.getinfo(1, "S").source:sub(2)
local tests_dir = this_file:match("(.*/)")
local plugin_root = tests_dir .. "../.."
vim.opt.rtp:prepend(plugin_root)
package.path = tests_dir .. "?.lua;" .. package.path

local test_files = {}
if vim.env.VF_TEST_FILE and vim.env.VF_TEST_FILE ~= "" then
  test_files[1] = vim.fn.fnamemodify(vim.env.VF_TEST_FILE, ":p")
else
  -- Discover test files. Skip `runner.lua` (this file) and anything
  -- underscore-prefixed (helpers, fixtures).
  local candidates = vim.fn.glob(tests_dir .. "*.lua", false, true)
  for _, path in ipairs(candidates) do
    local name = vim.fn.fnamemodify(path, ":t")
    if name ~= "runner.lua" and name:sub(1, 1) ~= "_" then
      test_files[#test_files + 1] = path
    end
  end
  table.sort(test_files)
end

for _, path in ipairs(test_files) do
  io.stdout:write("---- " .. vim.fn.fnamemodify(path, ":t") .. " ----\n")
  dofile(path)
end

io.stdout:write(string.format("\n%d passed, %d failed\n", passed, failed))
vim.cmd((failed == 0) and "cquit 0" or "cquit 1")
