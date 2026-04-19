local ffi_lib = require("vimficiency.ffi")

local function find_result(results, seq)
  for _, result in ipairs(results) do
    if result.seq == seq then
      return result
    end
  end
  return nil
end

test("ffi.analyze: preserves substitute payload and cost for exact 2->3 scenario", function()
  local initial_lines = {
    "int main() {",
    "  int m;",
    "  for(int i = 2; i < n; i++) {",
    "",
    "  }",
    "}",
  }
  local goal_lines = {
    "int main() {",
    "  int m;",
    "  for(int i = 3; i < n; i++) {",
    "",
    "  }",
    "}",
  }

  local results, user_cost = ffi_lib.analyze(
    initial_lines, goal_lines,
    0, 0,
    false, false,
    1, 2,
    2, 14,
    "jf;i<BS>3<Esc>",
    40, 20,
    20
  )

  assert_true(user_cost > 0, "user cost should parse as a positive number")

  local replace = find_result(results, "$Ef2r3")
  assert_true(replace ~= nil, "expected replace candidate $Ef2r3")
  assert_true(replace.cost > 0, "$Ef2r3 should keep a positive cost")

  local substitute = find_result(results, "$Ef2s3<Esc>")
  assert_true(substitute ~= nil,
    "expected substitute candidate $Ef2s3<Esc>; got " .. vim.inspect(results))
  assert_true(substitute.cost > 0,
    "$Ef2s3<Esc> should keep a positive cost, got " .. tostring(substitute.cost))

  local truncated = find_result(results, "$Ef2s")
  assert_true(truncated == nil,
    "truncated $Ef2s result must not appear after ffi parsing")
end)
