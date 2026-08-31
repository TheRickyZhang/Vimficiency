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

test("ffi._parse_analyze_results: reads the count-framed diffs section", function()
  local US = "\x1F"
  local payload = table.concat({
    "size: 1 user_cost: 2.000",
    "diffs: 0" .. US .. "1.500",   -- header-shaped seq; the count keeps it a result
    "diffs: 2",
    table.concat({ "0", "4", "0", "7", "0", "4", "0", "7" }, US),
    table.concat({ "1", "0", "1", "0", "1", "0", "2", "3" }, US),
    "----------------DEBUG----------------",
    table.concat({ "9", "9", "9", "9", "9", "9", "9", "9" }, US),  -- trailer noise, not a region
  }, "\n")

  local results, user_cost, diffs = ffi_lib._parse_analyze_results(payload)
  assert_eq(user_cost, 2.0)
  assert_eq(#results, 1)
  assert_eq(results[1].seq, "diffs: 0")
  assert_eq(#diffs, 2, "exactly the announced regions are read")
  assert_eq(diffs[1].init.begin_col, 4)
  assert_eq(diffs[1].goal.end_col, 7)
  assert_eq(diffs[2].goal.end_row, 2)
end)

test("ffi._parse_analyze_results: zero results still carries user_cost and diffs", function()
  local results, user_cost, diffs = ffi_lib._parse_analyze_results(
    "size: 0 user_cost: 5.250\ndiffs: 0\n")
  assert_eq(#results, 0)
  assert_eq(user_cost, 5.25)
  assert_eq(#diffs, 0)
end)

local function find_result(results, seq)
  for _, result in ipairs(results) do
    if result.seq == seq then
      return result
    end
  end
  return nil
end

local function find_result_ending_with(results, suffix)
  for _, result in ipairs(results) do
    if result.seq:sub(-#suffix) == suffix then
      return result
    end
  end
  return nil
end

local function with_reset_config(fn)
  ffi_lib.reset_config()
  local ok, err = pcall(fn)
  ffi_lib.reset_config()
  if not ok then error(err) end
end

test("ffi.configure: count penalty overrides affect analyze costs", function()
  with_reset_config(function()
    -- After reset_config every key costs 1.0 and only the count penalty is
    -- added on top, so `4w`/`4W` are the only two-key paths to col 19 and
    -- become the strict minimum once their penalty is zeroed. Asserting on
    -- that extreme (and its opposite) avoids pinning a ranked menu.
    local line = "one two three four five six"
    local function analyze()
      return ffi_lib.analyze(
        { line }, { line },
        0, #line - 1,
        false, false,
        0, 0,
        0, 19,
        "",
        40, 20,
        20
      )
    end

    local NO_PENALTY = { base = 0.0, count_slope = 0.0, span_slope = 0.0 }
    ffi_lib.configure({
      count_penalty_overrides = { MovementWord = NO_PENALTY, MovementBigWord = NO_PENALTY },
    })
    local free = analyze()
    local free_4w = find_result(free, "4w")
    local free_4W = find_result(free, "4W")
    assert_true(free_4w ~= nil and free_4W ~= nil,
      "zero count penalty must surface 4w and 4W; got " .. vim.inspect(free))
    assert_eq(free_4w.cost, 2.0, "4w with no count penalty costs its two keys")
    assert_eq(free_4W.cost, 2.0, "4W with no count penalty costs its two keys")
    assert_eq(free[1].cost, 2.0, "two-key counted motions must be the cheapest result")

    ffi_lib.configure({
      count_penalty_overrides = {
        MovementWord = { base = 60.0 },
        MovementBigWord = { base = 60.0 },
      },
    })
    local expensive = analyze()
    for _, seq in ipairs({ "4w", "4W" }) do
      local candidate = find_result(expensive, seq)
      assert_true(candidate == nil or candidate.cost > 60.0,
        seq .. " must be priced out or carry the 60.0 base; got " .. vim.inspect(candidate))
    end
  end)
end)

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

  local results, user_cost, _, diffs = ffi_lib.analyze(
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

  -- The planner's region for a one-character replacement is forced: `2` at
  -- (2,14) becomes `3`, both spans [14, 15) on row 2.
  assert_eq(#diffs, 1, "one planned region; got " .. vim.inspect(diffs))
  assert_eq(diffs[1].init.begin_row, 2); assert_eq(diffs[1].init.begin_col, 14)
  assert_eq(diffs[1].init.end_row, 2);   assert_eq(diffs[1].init.end_col, 15)
  assert_eq(diffs[1].goal.begin_row, 2); assert_eq(diffs[1].goal.begin_col, 14)
  assert_eq(diffs[1].goal.end_row, 2);   assert_eq(diffs[1].goal.end_col, 15)

  -- The nav prefix is whatever the search currently prefers; only the edit
  -- tail and its payload are the FFI contract under test.
  local replace = find_result_ending_with(results, "r3")
  assert_true(replace ~= nil,
    "expected a replace candidate ending in r3; got " .. vim.inspect(results))
  replace = assert(replace)
  assert_true(replace.cost > 0, replace.seq .. " should keep a positive cost")

  local substitute = find_result_ending_with(results, "s3<Esc>")
  assert_true(substitute ~= nil,
    "expected a substitute candidate ending in s3<Esc>; got " .. vim.inspect(results))
  substitute = assert(substitute)
  assert_true(substitute.cost > 0,
    substitute.seq .. " should keep a positive cost, got " .. tostring(substitute.cost))

  for _, result in ipairs(results) do
    assert_true(result.seq:sub(-1) ~= "s",
      "substitute payload must survive ffi parsing; got truncated " .. result.seq)
  end
end)

test("ffi.analyze_start_async: worker result matches sync analyze", function()
  local initial_lines = { "for(int i = 2; i < n; i++) {" }
  local goal_lines    = { "for(int i = 3; i < n; i++) {" }
  local last_col = #initial_lines[1] - 1

  local sync_results, sync_user_cost, _, sync_diffs = ffi_lib.analyze(
    initial_lines, goal_lines,
    0, last_col,
    false, false,
    0, 11,
    0, 11,
    "f2r3",
    40, 20,
    20
  )

  -- 0 = no deadline, so the worker runs to the same natural caps as sync.
  local job_id = ffi_lib.analyze_start_async(
    initial_lines, goal_lines,
    0, last_col,
    false, false,
    0, 11,
    0, 11,
    "f2r3",
    40, 20,
    20,
    nil, 0
  )

  local payload
  vim.wait(5000, function()
    payload = ffi_lib.analyze_poll(job_id)
    return payload ~= nil
  end, 10)
  assert_true(payload ~= nil, "async analyze must finish within the timeout")

  assert_eq(payload.user_cost, sync_user_cost, "user_cost must match the sync path")
  assert_eq(#payload.results, #sync_results, "result count must match the sync path")
  assert_true(#sync_diffs > 0, "a text change must plan at least one region")
  assert_true(vim.deep_equal(payload.diffs, sync_diffs),
    "worker diffs must match the sync path; got " .. vim.inspect(payload.diffs))

  local function to_map(results)
    local m = {}
    for _, r in ipairs(results) do m[r.seq] = r.cost end
    return m
  end
  local sync_map = to_map(sync_results)
  for _, r in ipairs(payload.results) do
    assert_true(sync_map[r.seq] ~= nil, "async produced seq absent from sync: " .. r.seq)
    assert_eq(r.cost, sync_map[r.seq], "cost mismatch for seq " .. r.seq)
  end
end)

test("ffi.analyze: accepts one empty physical line", function()
  local results, user_cost = ffi_lib.analyze(
    { "" }, { "" },
    0, 0,
    false, false,
    0, 0,
    0, 0,
    "",
    40, 20,
    1
  )

  assert_true(type(results) == "table", "results should parse")
  assert_true(user_cost >= 0, "user cost should parse")
end)

test("ffi.analyze: preserves trailing empty physical lines", function()
  local results, user_cost = ffi_lib.analyze(
    { "a", "" }, { "a", "" },
    0, 0,
    false, false,
    0, 0,
    1, 0,
    "j",
    40, 20,
    1
  )

  assert_true(type(results) == "table", "results should parse")
  assert_true(user_cost > 0, "user cost should include j")
end)

test("ffi.analyze: rejects empty line arrays", function()
  assert_error(function()
    ffi_lib.analyze(
      {}, { "" },
      0, 0,
      false, false,
      0, 0,
      0, 0,
      "",
      40, 20,
      1
    )
  end, "at least one line")
end)

test("ffi.analyze: rejects NUL bytes inside lines", function()
  assert_error(function()
    ffi_lib.analyze(
      { "a" .. string.char(0) .. "b" }, { "a" },
      0, 0,
      false, false,
      0, 0,
      0, 0,
      "",
      40, 20,
      1
    )
  end, "NUL byte")
end)
