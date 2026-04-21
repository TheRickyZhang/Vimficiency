-- tests/lua/stats/aggregate.lua
-- Pins the reducer: lifetime counters, efficiency geomean + daily, motion
-- ratios with normalize_molecule + the 10-occurrence threshold, beats
-- clamping at 100.

local stats = require("vimficiency.stats")

--- Build a log record with safe defaults. Overrides merge on top.
local function rec(overrides)
  local r = {
    v = 1,
    finish_epoch = 0,
    type = "mark",
    finish_reason = "manual",
    key_count = 0,
    user_cost = 1.0,
    best_opt_cost = 1.0,
    user_seq = "",
    best_opt_seq = "",
    beats = false,
  }
  for k, val in pairs(overrides or {}) do r[k] = val end
  return r
end

test("aggregate: empty log → zero counters, nil score", function()
  local s = stats.aggregate({})
  assert_eq(s.total_sessions, 0, "total_sessions")
  assert_eq(s.total_keys, 0, "total_keys")
  assert_eq(s.efficiency_score, nil, "efficiency_score")
  assert_eq(s.beats_count, 0, "beats_count")
  assert_eq(s.by_type, { mark = 0, watch = 0, recall = 0, suggest = 0 },
    "by_type all zero")
end)

test("aggregate: single exact-optimal session scores 100", function()
  local s = stats.aggregate({ rec({ user_cost = 5.0, best_opt_cost = 5.0 }) })
  assert_eq(s.total_sessions, 1)
  assert_true(math.abs(s.efficiency_score - 100) < 1e-6,
    "expected 100, got " .. tostring(s.efficiency_score))
end)

test("aggregate: single session with user 2× optimal → score = 50", function()
  local s = stats.aggregate({ rec({ user_cost = 10.0, best_opt_cost = 5.0 }) })
  assert_true(math.abs(s.efficiency_score - 50) < 1e-6,
    "expected 50, got " .. tostring(s.efficiency_score))
end)

test("aggregate: beats clamp at 100 and increment the dev counter", function()
  local s = stats.aggregate({
    rec({ user_cost = 2.0, best_opt_cost = 5.0, beats = true }),
  })
  assert_true(math.abs(s.efficiency_score - 100) < 1e-6,
    "beats must clamp to 100; got " .. tostring(s.efficiency_score))
  assert_eq(s.beats_count, 1)
end)

test("aggregate: by_type counts across mixed session types", function()
  local s = stats.aggregate({
    rec({ type = "mark" }),    rec({ type = "mark" }),
    rec({ type = "watch" }),
    rec({ type = "recall" }),  rec({ type = "recall" }),
    rec({ type = "suggest" }),
  })
  assert_eq(s.by_type.mark, 2)
  assert_eq(s.by_type.watch, 1)
  assert_eq(s.by_type.recall, 2)
  assert_eq(s.by_type.suggest, 1)
  assert_eq(s.total_sessions, 6)
end)

test("aggregate: efficiency geomean (not arithmetic mean) across records", function()
  -- Sessions with ratios 0.25 and 1.0 → scores 25 and 100.
  -- Geomean: sqrt(25 * 100) = 50.    Arithmetic would be 62.5.
  local s = stats.aggregate({
    rec({ user_cost = 4.0, best_opt_cost = 1.0 }),   -- score 25
    rec({ user_cost = 1.0, best_opt_cost = 1.0 }),   -- score 100
  })
  assert_true(math.abs(s.efficiency_score - 50) < 1e-6,
    "expected geomean 50, got " .. tostring(s.efficiency_score))
end)

test("aggregate: efficiency_by_day buckets by epoch day", function()
  local DAY = 86400
  local day_a = 5 * DAY + 100
  local day_b = 6 * DAY + 200
  local s = stats.aggregate({
    rec({ finish_epoch = day_a, user_cost = 2.0, best_opt_cost = 1.0 }),   -- 50
    rec({ finish_epoch = day_a, user_cost = 1.0, best_opt_cost = 1.0 }),   -- 100
    rec({ finish_epoch = day_b, user_cost = 4.0, best_opt_cost = 1.0 }),   -- 25
  })
  local day_a_idx = math.floor(day_a / DAY)
  local day_b_idx = math.floor(day_b / DAY)
  -- Geomean of 50, 100 = ~70.71.
  assert_true(math.abs(s.efficiency_by_day[day_a_idx] - math.sqrt(50 * 100)) < 1e-6)
  assert_true(math.abs(s.efficiency_by_day[day_b_idx] - 25) < 1e-6)
end)

test("normalize_molecule: collapses f/F/t/T/r/R trailing char", function()
  assert_eq(stats.normalize_molecule("fa"),  "f_")
  assert_eq(stats.normalize_molecule("Fb"),  "F_")
  assert_eq(stats.normalize_molecule("tc"),  "t_")
  assert_eq(stats.normalize_molecule("Td"),  "T_")
  assert_eq(stats.normalize_molecule("re"),  "r_")
  assert_eq(stats.normalize_molecule("Rf"),  "R_")
end)

test("normalize_molecule: preserves leading count on the collapsed form", function()
  assert_eq(stats.normalize_molecule("2fa"),  "2f_")
  assert_eq(stats.normalize_molecule("10Fa"), "10F_")
end)

test("normalize_molecule: leaves operator+motion pairs, counts, etc. alone", function()
  assert_eq(stats.normalize_molecule("dw"),   "dw")
  assert_eq(stats.normalize_molecule("d$"),   "d$")
  assert_eq(stats.normalize_molecule("ciw"),  "ciw")
  assert_eq(stats.normalize_molecule("3w"),   "3w")
  assert_eq(stats.normalize_molecule("x"),    "x")
  assert_eq(stats.normalize_molecule("dd"),   "dd")
end)

--- The FFI tokenizer is available during tests (it's loaded as part of
--- vimficiency.ffi). We use it indirectly by feeding simple sequences
--- that tokenize into predictable single-atom results.
test("aggregate: motion_ratios respects the 10-occurrence threshold", function()
  -- Ten sessions each with a single 'w' in user_seq and a single 'e' in
  -- best_opt_seq → 'w' appears 10 user-side (meets threshold), 'e'
  -- appears 10 opt-side (meets threshold).
  local records = {}
  for _ = 1, 10 do
    records[#records + 1] = rec({ user_seq = "w", best_opt_seq = "e" })
  end
  local s = stats.aggregate(records)
  assert_true(s.motion_ratios["w"] ~= nil, "'w' should clear threshold")
  assert_true(s.motion_ratios["e"] ~= nil, "'e' should clear threshold")
end)

test("aggregate: motion below threshold is dropped", function()
  -- Only 9 combined occurrences of 'w' → below 10-threshold.
  local records = {}
  for _ = 1, 9 do
    records[#records + 1] = rec({ user_seq = "w", best_opt_seq = "" })
  end
  local s = stats.aggregate(records)
  assert_eq(s.motion_ratios["w"], nil, "'w' must be filtered below threshold")
end)

test("aggregate: user-only molecule gets ratio = math.huge", function()
  -- 10 user-side 'w', 0 opt-side → denominator zero.
  local records = {}
  for _ = 1, 10 do
    records[#records + 1] = rec({ user_seq = "w", best_opt_seq = "" })
  end
  local s = stats.aggregate(records)
  assert_true(s.motion_ratios["w"] ~= nil, "'w' should exist")
  assert_eq(s.motion_ratios["w"].ratio, math.huge,
    "optimizer never suggests → ratio must be math.huge")
end)
