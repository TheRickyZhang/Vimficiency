local ffi_lib = require("vimficiency.ffi")

test("ffi._parse_analyze_results: preserves literal whitespace inside the sequence", function()
  -- C++ `AnalyzeExports.cpp` emits `<raw_seq_bytes>\x1F<cost>\n`. Captured
  -- insert-mode text can contain spaces, tabs, or other bytes; none collide
  -- with the Unit Separator (0x1F), which the user cannot type.
  local US = "\x1F"
  local payload = table.concat({
    "size: 4 user_cost: 12.345",
    "ihello world\27" .. US .. "3.500",        -- embedded space + <Esc>
    "ifoo bar baz\27" .. US .. "5.250",        -- multiple embedded spaces
    "ia\tb\27" .. US .. "2.000",               -- embedded tab
    "$Ef2r3" .. US .. "1.125",                 -- no embedded whitespace
  }, "\n")

  local results, user_cost = ffi_lib._parse_analyze_results(payload)

  assert_eq(user_cost, 12.345, "user_cost should parse from header")
  assert_eq(#results, 4, "should parse all four result lines")

  assert_eq(results[1].seq, "ihello world\27",
    "first seq must retain its embedded space and <Esc>")
  assert_eq(results[1].cost, 3.500)

  assert_eq(results[2].seq, "ifoo bar baz\27",
    "second seq must retain all embedded spaces")
  assert_eq(results[2].cost, 5.250)

  assert_eq(results[3].seq, "ia\tb\27",
    "third seq must retain an embedded tab")
  assert_eq(results[3].cost, 2.000)

  assert_eq(results[4].seq, "$Ef2r3", "baseline (no embedded whitespace) unchanged")
  assert_eq(results[4].cost, 1.125)
end)

test("ffi._parse_analyze_results: stops at the DEBUG separator", function()
  local US = "\x1F"
  local payload = table.concat({
    "size: 1 user_cost: 2.000",
    "abc" .. US .. "1.500",
    "----------------DEBUG----------------",
    "noise line" .. US .. "99.999",   -- would parse as a fake result without the guard
  }, "\n")

  local results = ffi_lib._parse_analyze_results(payload)
  assert_eq(#results, 1, "must stop at DEBUG marker")
  assert_eq(results[1].seq, "abc")
end)

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
  replace = assert(replace)
  assert_true(replace.cost > 0, "$Ef2r3 should keep a positive cost")

  local substitute = find_result(results, "$Ef2s3<Esc>")
  assert_true(substitute ~= nil,
    "expected substitute candidate $Ef2s3<Esc>; got " .. vim.inspect(results))
  substitute = assert(substitute)
  assert_true(substitute.cost > 0,
    "$Ef2s3<Esc> should keep a positive cost, got " .. tostring(substitute.cost))

  local truncated = find_result(results, "$Ef2s")
  assert_true(truncated == nil,
    "truncated $Ef2s result must not appear after ffi parsing")
end)
