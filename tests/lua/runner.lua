-- Minimal Lua test runner.
-- Run with `nvim --headless -l tests/lua/runner.lua` or `./tests/lua/run.sh`.

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
  -- Discover test files recursively, skipping this runner and `_` helpers.
  local candidates = vim.fn.glob(tests_dir .. "**/*.lua", false, true)
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
