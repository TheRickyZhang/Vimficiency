-- tests/lua/runner.lua
-- Minimal test runner for vimficiency Lua modules.
--
-- Invocation: nvim --headless -l tests/lua/runner.lua
--   or equivalently: ./tests/lua/run.sh
--
-- Each test_*.lua file in this directory is loaded in sequence. Test
-- files register cases via the globally-exposed `test(name, fn)` and use
-- `assert_eq(actual, expected [, msg])` (or any pcall-friendly assertion)
-- for checks. Exit code is zero only if every test passed.
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
  if actual ~= expected then
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

-- Locate this file, point runtimepath at the plugin root so `require`
-- finds lua/vimficiency/*.
local this_file = debug.getinfo(1, "S").source:sub(2)
local tests_dir = this_file:match("(.*/)")
local plugin_root = tests_dir .. "../.."
vim.opt.rtp:prepend(plugin_root)

-- Discover test files (alphabetical order, so runs are deterministic).
local test_files = vim.fn.glob(tests_dir .. "test_*.lua", false, true)
table.sort(test_files)

for _, path in ipairs(test_files) do
  io.stdout:write("---- " .. vim.fn.fnamemodify(path, ":t") .. " ----\n")
  dofile(path)
end

io.stdout:write(string.format("\n%d passed, %d failed\n", passed, failed))
vim.cmd((failed == 0) and "cquit 0" or "cquit 1")
