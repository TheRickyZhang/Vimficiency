-- tests/lua/stats/log.lua
-- Round-trip and crash-safety for the JSONL log.

local stats_log = require("vimficiency.stats.log")
local h = require("_helpers")

--- Point stdpath("data") at a fresh tempdir so the log file sits in an
--- isolated location for this test. The runner already points
--- XDG_DATA_HOME at a shared tempdir, but we want per-test isolation so
--- multiple tests here don't contaminate each other.
local function use_temp_data_dir()
  local tmp = vim.fn.tempname()
  vim.fn.mkdir(tmp, "p")
  vim.env.XDG_DATA_HOME = tmp
  return tmp
end

local function sample_record(overrides)
  local r = {
    v = 1,
    finish_epoch = 1234567,
    type = "mark",
    finish_reason = "manual",
    key_count = 4,
    user_cost = 3.0,
    best_opt_cost = 2.0,
    user_seq = "dw",
    best_opt_seq = "de",
    beats = false,
  }
  for k, val in pairs(overrides or {}) do r[k] = val end
  return r
end

test("stats.log: append + read_all round-trips records", function()
  use_temp_data_dir()
  stats_log.append(sample_record({ user_seq = "a" }))
  stats_log.append(sample_record({ user_seq = "b" }))
  stats_log.append(sample_record({ user_seq = "c" }))

  local records, skipped = stats_log.read_all()
  assert_eq(#records, 3, "expected 3 records")
  assert_eq(skipped, 0, "no lines should be skipped")
  assert_eq(records[1].user_seq, "a")
  assert_eq(records[2].user_seq, "b")
  assert_eq(records[3].user_seq, "c")
end)

test("stats.log: read_all on missing file returns empty list", function()
  use_temp_data_dir()
  local records, skipped = stats_log.read_all()
  assert_eq(#records, 0)
  assert_eq(skipped, 0)
end)

test("stats.log: malformed trailing line is silently skipped", function()
  use_temp_data_dir()
  stats_log.append(sample_record({ user_seq = "ok" }))
  -- Simulate a crash mid-write by appending an unterminated / invalid line.
  local fh = assert(io.open(stats_log._log_path(), "a"))
  fh:write("{not json at all\n")
  fh:write("   \n")                  -- whitespace-only line (decodes ok? skip)
  fh:write("{\"partial\":")           -- no newline, truncated
  fh:close()

  local records, skipped = stats_log.read_all()
  assert_eq(#records, 1, "only the well-formed record should survive")
  assert_eq(records[1].user_seq, "ok")
  assert_true(skipped >= 1, "at least one malformed line must be counted as skipped")
end)

test("stats.log: build_record flags beats when user beats optimizer", function()
  local r = stats_log.build_record("mark", h.fake_result({
    user_seq = "xyz",
    user_cost = 1.0,
    optimal_results = { { seq = "abc", cost = 5.0 } },
    finish_reason = "manual",
    key_count = 3,
  }))
  r = assert(r)
  assert_true(r.beats, "user_cost < best_opt_cost must flag beats")
  assert_eq(r.best_opt_cost, 5.0)
  assert_eq(r.best_opt_seq, "abc")
end)

test("stats.log: build_record picks the lowest-cost optimal entry", function()
  local r = stats_log.build_record("mark", h.fake_result({
    user_seq = "xyz",
    user_cost = 10.0,
    optimal_results = {
      { seq = "hi",  cost = 8.0 },
      { seq = "lo",  cost = 2.0 },
      { seq = "mid", cost = 5.0 },
    },
    finish_reason = "manual",
    key_count = 3,
  }))
  r = assert(r)
  assert_eq(r.best_opt_cost, 2.0)
  assert_eq(r.best_opt_seq, "lo")
end)

test("stats.log: build_record returns nil when result is missing user_seq", function()
  local result = h.fake_result({ user_cost = 1.0 })
  result.user_seq = nil
  ---@diagnostic disable-next-line: param-type-mismatch
  local r = stats_log.build_record("mark", result)
  assert_eq(r, nil)
end)
