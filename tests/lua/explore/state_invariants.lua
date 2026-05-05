-- State-invariant tests for the explore plugin.
--
-- The headline assertion: after `helpers.feed` returns, the plugin's
-- `state.phase.kind` is consistent with `nvim_get_mode().mode`. This file
-- names that invariant explicitly (it is otherwise only encoded implicitly,
-- via `mode_is_insert()` in handlers.lua) and exercises it from two
-- directions: hard-coded regression cases known to expose drift, and a
-- bounded grammar-driven fuzzer.
--
-- Limitations to be honest about up front:
--
--   * `helpers.feed` triggers an *implicit* InsertLeave when the fed
--     sequence ends mid-insert (see _helpers.lua docstring). That means
--     this harness CANNOT directly observe "Neovim is in mode i right
--     now" — by the time we read `nvim_get_mode()`, the implicit leave
--     has already fired. So this file covers POST-action drift, not
--     mid-insert drift. The user-reported `wdiwi`-with-recs-wrong bug
--     manifests mid-insert; if the same bug also leaves the plugin in a
--     corrupted state after `<Esc>`, this catches it. If it's purely
--     transient, a different kind of harness (scripted input over time)
--     is needed.
--   * Recommendations are not asserted — they're output, not state.
--   * Visual / command-line / terminal modes are excluded from the
--     fuzzer. Explore's behavior in those modes isn't pinned down yet.

local helpers = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore = require("vimficiency.explore")

local v = vim.api

-- ===========================================================================
-- Layer 1: the invariant.
-- ===========================================================================

-- Returns (ok, msg). When ok is false, msg explains the violation. The
-- predicate intentionally accepts both Navigate and Transform for
-- non-insert-like modes; the C++ backend chooses between them based on
-- buffer state, and the test cannot reproduce that decision without
-- duplicating the backend logic.
local function check_mode_phase(mode, phase_kind)
  local first = mode:sub(1, 1)
  local insert_like = first == "i" or first == "R"
  if insert_like then
    if phase_kind == "Insert" then return true end
    return false, ("mode=%q (insert-like) but phase=%q; expected Insert")
      :format(mode, phase_kind)
  end
  if phase_kind == "Navigate" or phase_kind == "Transform" then
    return true
  end
  return false, ("mode=%q (normal-like) but phase=%q; expected Navigate or Transform")
    :format(mode, phase_kind)
end

-- ===========================================================================
-- Harness: feed a sequence, snapshot, assert the invariant + cursor /
-- buffer consistency between Neovim and the plugin's tracked state.
-- ===========================================================================

local function snapshot(scratch_buf, scratch_win)
  local cur = v.nvim_win_get_cursor(scratch_win)
  return {
    mode = v.nvim_get_mode().mode,
    cursor = { row = cur[1] - 1, col = cur[2] },
    lines = v.nvim_buf_get_lines(scratch_buf, 0, -1, false),
    status = explore.status(),
  }
end

local function lines_equal(a, b)
  if #a ~= #b then return false end
  for i = 1, #a do
    if a[i] ~= b[i] then return false end
  end
  return true
end

-- Returns nil on success; on failure returns a self-contained diagnostic
-- string (sequence, mode, phase, cursors, lines).
local function check_consistency(seq, snap)
  local violations = {}

  local ok, msg = check_mode_phase(snap.mode, snap.status.phase.kind)
  if not ok then violations[#violations + 1] = "  * " .. msg end

  if snap.status.cursor.row ~= snap.cursor.row
     or snap.status.cursor.col ~= snap.cursor.col then
    violations[#violations + 1] = ("  * cursor drift: nvim=(%d,%d) plugin=(%d,%d)")
      :format(snap.cursor.row, snap.cursor.col,
              snap.status.cursor.row, snap.status.cursor.col)
  end

  if not lines_equal(snap.lines, snap.status.session_lines) then
    violations[#violations + 1] = ("  * buffer drift:\n      nvim=%s\n      plugin=%s")
      :format(vim.inspect(snap.lines), vim.inspect(snap.status.session_lines))
  end

  if #violations == 0 then return nil end
  return ("seq=%q mode=%q phase=%q\n%s"):format(
    seq, snap.mode, snap.status.phase.kind,
    table.concat(violations, "\n"))
end

-- The fixture buffer is shared by every case: realistic enough to exercise
-- word/operator motions but small enough that the test stays fast.
local function fresh_result()
  return explore_helpers.fake_result({
    lines = { "int n = 10;" },
    goal_lines = { "int n = 10;" },
    user_seq = "x",
    optimal_results = { { seq = "x", cost = 1.0 } },
  })
end

local function run_case(label, seq)
  helpers.silence_notify(function()
    explore_helpers.open_flow(label, fresh_result(), function(scratch_buf)
      local scratch_win = v.nvim_get_current_win()
      helpers.feed(seq)
      vim.wait(1000, function()
        local status, view = pcall(require("vimficiency.explore.registry").current)
        return status and view.restoring == nil
      end, 10)
      -- Headless artifact: `nvim_feedkeys` in `xt` mode doesn't reliably
      -- fire CursorMoved for pure-motion sequences; the plugin's cursor
      -- tracking would lag behind despite working fine in real interactive
      -- use. Match flow.lua's pattern and force the autocmd so the comparison
      -- reflects the plugin's normal post-event state.
      explore_helpers.trigger_cursor_moved(scratch_buf)
      local snap = snapshot(scratch_buf, scratch_win)
      local err = check_consistency(seq, snap)
      assert_true(err == nil,
        "explore state diverged from Neovim:\n" .. (err or ""))
    end)
  end)
end

-- ===========================================================================
-- Layer 2: deterministic regression cases.
--
-- Sequences here all end in normal mode (or land cleanly there) so the
-- implicit-InsertLeave quirk doesn't affect observation. The user-reported
-- `wdiwi` case is captured here as the trailing `<Esc>` form — if the
-- bug leaves residual corruption after the insert finishes, this fires.
-- ===========================================================================

local CASES = {
  { name = "wdiw-then-insert-then-esc",  seq = "wdiwiabc<Esc>" },
  { name = "diw-then-insert-then-esc",   seq = "diwiabc<Esc>" },
  { name = "ciw-then-typed-then-esc",    seq = "ciwhello<Esc>" },
  { name = "operator-cancel-via-esc",    seq = "d<Esc>" },
  { name = "motion-only",                seq = "wlh" },
}

for _, case in ipairs(CASES) do
  test("invariant: " .. case.name, function()
    run_case("invariants-" .. case.name, case.seq)
  end)
end

-- ===========================================================================
-- Layer 3: bounded grammar fuzzer.
--
-- Generates short Vim sequences from a small atom set, runs each through
-- the same harness. Fixed seed + small N for CI stability and easy repro.
-- On failure, the seed and the offending sequence are in the diagnostic.
-- ===========================================================================

local FUZZ_SEED = 1
local FUZZ_N = 30
local MIN_ATOMS = 2
local MAX_ATOMS = 5

local ATOMS = {
  motion   = { "h", "l", "j", "k", "w", "e", "b", "ge", "0", "$" },
  operator = { "d", "c", "y" },
  textobj  = { "iw", "aw" },
  enter_i  = { "i", "I", "a", "A", "o", "O" },
  typed    = { "x", "yz", "abc" },
}

local function pick(list, rng)
  return list[rng:random(1, #list)]
end

-- Each atom is a complete Vim "thought": a motion, a finished operator
-- application, or a finished insert. All atoms END IN NORMAL MODE so the
-- composition stays parseable and the implicit-leave quirk is a no-op.
local function gen_atom(rng)
  local kind = rng:random(1, 4)
  if kind == 1 then
    return pick(ATOMS.motion, rng)
  elseif kind == 2 then
    return pick(ATOMS.operator, rng) .. pick(ATOMS.motion, rng)
  elseif kind == 3 then
    return pick(ATOMS.operator, rng) .. pick(ATOMS.textobj, rng)
  else
    return pick(ATOMS.enter_i, rng) .. pick(ATOMS.typed, rng) .. "<Esc>"
  end
end

local function gen_sequence(rng)
  local n = rng:random(MIN_ATOMS, MAX_ATOMS)
  local parts = {}
  for i = 1, n do parts[i] = gen_atom(rng) end
  return table.concat(parts)
end

test("invariant: fuzz", function()
  -- Use a private RNG state so the fuzzer doesn't perturb global math.random.
  local rng = { state = FUZZ_SEED }
  function rng:random(lo, hi)
    -- LCG (Numerical Recipes constants) — small, deterministic, no math.random dep.
    self.state = (self.state * 1103515245 + 12345) % 2147483648
    return lo + (self.state % (hi - lo + 1))
  end

  for i = 1, FUZZ_N do
    local seq = gen_sequence(rng)
    local label = string.format("fuzz-%d", i)
    -- Wrap in pcall so a single fuzz failure produces a diagnostic with the
    -- offending sequence and seed instead of bailing on the first failure.
    local ok, err = pcall(run_case, label, seq)
    assert_true(ok, string.format(
      "fuzz failure (seed=%d, iter=%d, seq=%q):\n%s",
      FUZZ_SEED, i, seq, tostring(err)))
  end
end)
