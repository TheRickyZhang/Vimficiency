-- This file lives at tests/lua/simulate/integration.lua, so the plugin
-- root is three levels up and `_helpers` lives in tests/lua/.
local script = debug.getinfo(1, "S").source:sub(2)
local this_dir = script:match("(.*/)")
local tests_root = this_dir .. "../"
local plugin_root = this_dir .. "../../.."
vim.opt.rtp:prepend(plugin_root)
package.path = tests_root .. "?.lua;" .. package.path

local helpers = require("_helpers")
local sim = require("vimficiency.simulate")
local session = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

-- Quiet the environment — mirrors `runner.lua`'s treatment so this test
-- fits the same output shape as the main batch. See the runner for
-- rationale.
local verbose = vim.env.VF_TEST_VERBOSE == "1"
if not verbose then
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.notify = function(...) end
end
vim.o.showmode = false

local passed = 0
local failed = 0
---@type { name: string, err: string }[]
local failed_list = {}

---@param actual any
---@param expected any
---@param msg string
local function assert_eq(actual, expected, msg)
  if not vim.deep_equal(actual, expected) then
    error(string.format("%s: expected %s, got %s",
      msg, vim.inspect(expected), vim.inspect(actual)), 2)
  end
end

---@param cond any
---@param msg string
local function assert_true(cond, msg)
  if not cond then error(msg, 2) end
end

---@param name string
local function pass(name)
  passed = passed + 1
  if verbose then
    io.stdout:write("[       OK ] integration :: " .. name .. "\n")
  end
end

---@param name string
---@param err any
local function fail(name, err)
  failed = failed + 1
  failed_list[#failed_list + 1] = { name = name, err = tostring(err) }
  io.stderr:write(string.format(
    "[  FAILED  ] integration :: %s\n    %s\n",
    name, tostring(err):gsub("\n", "\n    ")))
end

---@param idx integer
---@return VF.Replay.Window
local function get_window(idx)
  local entry = sim._debug_get_windows()[idx]
  assert_true(entry ~= nil, "missing replay window " .. idx)
  return entry
end

---@param idx integer
---@param step integer
---@return VF.Replay.Snapshot
local function get_snapshot(idx, step)
  local seq_states = sim._debug_get_states()[idx]
  assert_true(seq_states ~= nil, "missing replay states for window " .. idx)
  local snap = seq_states[step + 1]
  assert_true(snap ~= nil, string.format("missing replay snapshot %d for window %d", step, idx))
  return snap
end

---@param buf integer
---@return string[]
local function header_lines(buf)
  local marks = vim.api.nvim_buf_get_extmarks(buf, -1, 0, -1, { details = true })
  for _, mark in ipairs(marks) do
    local details = mark[4]
    if details.virt_lines then
      ---@type string[]
      local lines = {}
      for _, row in ipairs(details.virt_lines) do
        local parts = {}
        for _, chunk in ipairs(row) do
          parts[#parts + 1] = chunk[1]
        end
        lines[#lines + 1] = table.concat(parts)
      end
      return lines
    end
  end
  error("missing virtual header")
end

---@param sequences string[]
---@param lines string[]
---@param fn fun()
---@param next fun(ok: boolean, err: any)
---@param opts { start_row: integer?, start_col: integer?, user_seq: string? }?   0-indexed; default (0, 0)
local function with_replay(sequences, lines, fn, next, opts)
  local prev_notify = vim.notify
  local prev_echo = vim.api.nvim_echo
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.notify = function(...) end
  ---@diagnostic disable-next-line: duplicate-set-field
  vim.api.nvim_echo = function(...) end

  local start_row = (opts and opts.start_row) or 0
  local start_col = (opts and opts.start_col) or 0
  local user_seq = opts and opts.user_seq or nil

  helpers.new_buf({ "simulate source" })
  vim.api.nvim_win_set_cursor(0, { 1, 0 })
  helpers.feed("<Esc>")

  local suggestions = {}
  for _, seq in ipairs(sequences) do
    suggestions[#suggestions + 1] = { seq = seq }
  end
  -- Seed play settings so every pane in the test set is visible. Without
  -- this the tests would be clamped to the default window_count (2).
  local play = require("vimficiency.play")
  local expected_windows = #suggestions + (user_seq and 1 or 0)
  play.set_setting("window_count", math.min(4, math.max(1, expected_windows)))
  play.set_setting("include_user_sequence", user_seq ~= nil)
  sim.simulate_compare(lines, start_row, start_col,
    {
      user = user_seq and { seq = user_seq } or nil,
      suggestions = suggestions,
    })

  local tries = 0
  local function finish(ok, err)
    sim.cleanup_compare()
    vim.notify = prev_notify
    vim.api.nvim_echo = prev_echo
    next(ok, err)
  end

  local function wait_ready()
    tries = tries + 1
    local windows = sim._debug_get_windows()
    local states = sim._debug_get_states()
    if #windows == expected_windows and #states == expected_windows then
      local ok, err = pcall(fn)
      finish(ok, err)
    elseif tries > 300 then
      finish(false, "simulate_compare did not finish precompute")
    else
      vim.defer_fn(wait_ready, 10)
    end
  end

  wait_ready()
end

---@param name string
---@return table
local function load_vimficiency_file(name)
  local path = plugin_root .. "/data/VimficiencyFiles/" .. name .. ".json"
  local fh = assert(io.open(path, "r"))
  local text = fh:read("*a")
  fh:close()
  return vim.json.decode(text)
end

---@type { name: string, run: fun(next: fun(ok: boolean, err: any)) }[]
local cases = {
  {
    name = "simulate tokenization merges feedable commands",
    run = function(next)
      next(pcall(function()
        -- Tokens are `{text, kind}` records after the FFI metadata refactor;
        -- extract `.text` for the split-shape assertions below. Kind
        -- assertions live in the standalone `tokenize.lua` test.
        local function texts(seq)
          local out = {}
          for _, t in ipairs(sim._debug_tokenize_for_animation(seq)) do
            out[#out + 1] = t.text
          end
          return table.concat(out, "|")
        end
        assert_eq(texts("jf;i<BS>3<Esc><Space>ve"),
          "j|f;|i|<BS>|3|<Esc>|<Space>|v|e", "tokenized insert/visual sequence")
        assert_eq(texts("3wfa;ww"),
          "3w|fa;|w|w", "tokenized motion sequence")
        assert_eq(texts("<Space>ww"),
          "<Space>|w|w", "tokenized leading space")
        assert_eq(texts("Axyz<Esc>"),
          "A|xyz|<Esc>", "tokenized append-at-eol")
        assert_eq(texts("A2<Space>*<Space>i<Esc>"),
          "A|2|<Space>|*|<Space>|i|<Esc>", "tokenized insert-mode spaces")
        assert_eq(texts("Afoo<CR>bar<Esc>"),
          "A|foo|<CR>|bar|<Esc>", "tokenized insert-mode CR")
      end))
    end,
  },
  {
    name = "simulate replays insert and visual sequence with virtual header",
    run = function(next)
      with_replay({ "jf;i<BS>3<Esc><Space>ve", "j" }, {
        "one two",
        "abc;def ghi",
        "last line",
      }, function()
        local seq1 = get_window(1)
        assert_eq(get_snapshot(1, 3).cursor, { 2, 3 }, "snapshot cursor after entering insert")
        assert_eq(get_snapshot(1, 3).mode, "i", "snapshot mode after entering insert")

        sim._debug_seek_to(3)
        assert_true(header_lines(seq1.buf)[3]:find("Mode INSERT") ~= nil,
          "insert header on mode row")

        assert_eq(get_snapshot(1, 4).lines,
          { "one two", "ab;def ghi", "last line" }, "snapshot lines after <BS>")
        sim._debug_seek_to(4)
        assert_eq(vim.api.nvim_buf_get_lines(seq1.buf, 0, -1, true),
          { "one two", "ab;def ghi", "last line" }, "rendered lines after <BS>")

        assert_eq(get_snapshot(1, 5).lines,
          { "one two", "ab3;def ghi", "last line" }, "snapshot lines after typed 3")
        sim._debug_seek_to(5)
        assert_eq(vim.api.nvim_buf_get_lines(seq1.buf, 0, -1, true),
          { "one two", "ab3;def ghi", "last line" }, "rendered lines after typed 3")

        assert_eq(get_snapshot(1, 8).mode, "v", "snapshot mode after visual enter")
        sim._debug_seek_to(8)
        assert_true(header_lines(seq1.buf)[3]:find("Mode VISUAL") ~= nil,
          "visual header on mode row")
        assert_eq(get_snapshot(1, 9).cursor, { 2, 6 }, "snapshot cursor after visual e")
      end, next)
    end,
  },
  {
    name = "simulate handles leading space and append-at-eol",
    run = function(next)
      with_replay({ "3wfa;ww", "<Space>ww", "Axyz<Esc>" }, {
        "alpha beta; gamma",
        "delta epsilon",
        "zeta",
      }, function()
        local seq3 = get_window(3)
        assert_eq(get_snapshot(2, 3).cursor, { 1, 10 }, "snapshot cursor after leading-space replay")
        assert_eq(get_snapshot(3, 3).cursor, { 1, 19 }, "snapshot append-at-eol cursor")
        sim._debug_seek_to(3)
        assert_eq(vim.api.nvim_buf_get_lines(seq3.buf, 0, -1, true),
          { "alpha beta; gammaxyz", "delta epsilon", "zeta" }, "rendered append-at-eol lines")
      end, next)
    end,
  },
  {
    name = "simulate replays insert-mode key notation as typed characters",
    run = function(next)
      local sequences = {
        "A2<Space>*<Space>i<Esc>",
        "Afoo<CR>bar<Esc>",
        "feciw2<Space>*<Space>i<Esc>",
      }
      with_replay(sequences, { "cout << endl;" }, function()
        local steps1 = #sim._debug_tokenize_for_animation(sequences[1])
        local steps2 = #sim._debug_tokenize_for_animation(sequences[2])
        local steps3 = #sim._debug_tokenize_for_animation(sequences[3])
        assert_eq(get_snapshot(1, steps1).lines,
          { "cout << endl;2 * i" }, "insert-mode <Space> should replay as spaces")
        assert_eq(get_snapshot(2, steps2).lines,
          { "cout << endl;foo", "bar" }, "insert-mode <CR> should replay as newline")
        assert_eq(get_snapshot(3, steps3).lines,
          { "cout << 2 * i;" }, "insert-mode key notation after ciw should replay fully")
      end, next)
    end,
  },
  {
    name = "simulate replays key notation after dot-repeat and ciw",
    run = function(next)
      local key_notation = "x.ciw2<Space>*<Space>i<Esc>"
      local legacy_raw = "x.ciw2 * i<Esc>"
      with_replay({ key_notation, legacy_raw }, { "cout << i+1 << endl;" }, function()
        local notation_steps = #sim._debug_tokenize_for_animation(key_notation)
        local legacy_steps = #sim._debug_tokenize_for_animation(legacy_raw)
        assert_eq(get_snapshot(1, notation_steps).lines,
          { "cout << 2 * i << endl;" },
          "key-notation spaces after x.ciw should replay fully")
        assert_eq(get_snapshot(2, legacy_steps).lines,
          { "cout << 2 * i << endl;" },
          "legacy raw spaces after x.ciw should replay fully")
      end, next, { start_col = 8 })
    end,
  },
  {
    name = "simulate replays saved-session dot-repeat ciw sequence",
    run = function(next)
      local lines = {
        "#include <bits/stdc++.h>",
        "using namespace std;",
        "",
        "int main() {",
        "  int n = 1;",
        "  for(int i = 0; i < n; i++) {",
        "    cout << i+1 << endl;",
        "  }",
        "}",
      }
      local seq = "Wjwrm$i0<Esc>jfnrmjFix.ciw2<Space>*<Space>i<Esc>"
      with_replay({ seq }, lines, function()
        local steps = #sim._debug_tokenize_for_animation(seq)
        local snap = get_snapshot(1, steps)
        assert_eq(snap.lines[7],
          "    cout << 2 * i << endl;",
          "saved-session opt2 snapshot should contain full typed tail")
        local win = get_window(1)
        sim._debug_seek_to(steps)
        assert_eq(vim.api.nvim_buf_get_lines(win.buf, 6, 7, true)[1],
          "    cout << 2 * i << endl;",
          "saved-session opt2 rendered buffer should contain full typed tail")
      end, next, { start_row = 3, start_col = 0 })
    end,
  },
  {
    name = "simulate keeps finished suggestion rendered while user pane continues",
    run = function(next)
      local lines = {
        "#include <bits/stdc++.h>",
        "using namespace std;",
        "",
        "int main() {",
        "  int n = 1;",
        "  for(int i = 0; i < n; i++) {",
        "    cout << i+1 << endl;",
        "  }",
        "}",
      }
      local user_seq = "jwwrmf;i0<Esc>jfnrmjBBBcWi<BS>2<Space>*<Space>i<Esc>"
      local opt1 = "Wjwrm$i0<Esc>jfnrmjF+Xce2<Space>*<Space>i<Esc>"
      local opt2 = "Wjwrm$i0<Esc>jfnrmjFix.ciw2<Space>*<Space>i<Esc>"
      with_replay({ opt1, opt2 }, lines, function()
        local user_steps = #sim._debug_tokenize_for_animation(user_seq)
        sim._debug_seek_to(user_steps)
        local windows = sim._debug_get_windows()
        assert_eq(vim.api.nvim_buf_get_lines(windows[3].buf, 6, 7, true)[1],
          "    cout << 2 * i << endl;",
          "finished opt2 pane should keep full final buffer while user pane continues")
      end, next, {
        start_row = 3,
        start_col = 0,
        user_seq = user_seq,
      })
    end,
  },
  {
    name = "session simulate replays saved-session dot-repeat ciw sequence",
    run = function(next)
      local lines = {
        "#include <bits/stdc++.h>",
        "using namespace std;",
        "",
        "int main() {",
        "  int n = 1;",
        "  for(int i = 0; i < n; i++) {",
        "    cout << i+1 << endl;",
        "  }",
        "}",
      }
      local user_seq = "jwwrmf;i0<Esc>jfnrmjBBBcWi<BS>2<Space>*<Space>i<Esc>"
      local opt1 = "Wjwrm$i0<Esc>jfnrmjF+Xce2<Space>*<Space>i<Esc>"
      local opt2 = "Wjwrm$i0<Esc>jfnrmjFix.ciw2<Space>*<Space>i<Esc>"
      local result = {
        lines = lines,
        start_row = 3,
        start_col = 0,
        end_row = 6,
        end_col = 16,
        user_seq = user_seq,
        user_cost = 29,
        optimal_results = {
          { seq = opt1, cost = 26 },
          { seq = opt2, cost = 28 },
        },
        start_time = 1,
        key_count = 1,
        timestamp = 2,
      }

      helpers.new_buf({ "session simulate source" })
      local id = assert(session_store.register_fetched_result("simreplay", result))
      local play = require("vimficiency.play")
      play.set_setting("include_user_sequence", true)
      play.set_setting("window_count", 3)
      session.simulate("simreplay", 3)

      local tries = 0
      local function finish(ok, err)
        sim.cleanup_compare()
        session_store.remove(id)
        next(ok, err)
      end

      local function wait_ready()
        tries = tries + 1
        local windows = sim._debug_get_windows()
        local states = sim._debug_get_states()
        if #windows == 3 and #states == 3 then
          local ok, err = pcall(function()
            local user = sim._debug_get_pool().user
            assert_true(user ~= nil, "missing user replay pool entry")
            sim._debug_seek_to(#user.tokens)
            assert_eq(vim.api.nvim_buf_get_lines(windows[2].buf, 6, 7, true)[1],
              "    cout << 2 * i << endl;",
              "session.simulate opt1 pane should render the full typed tail")
            assert_eq(vim.api.nvim_buf_get_lines(windows[3].buf, 6, 7, true)[1],
              "    cout << 2 * i << endl;",
              "session.simulate opt2 pane should render the full typed tail")
          end)
          finish(ok, err)
        elseif tries > 300 then
          finish(false, "session.simulate did not finish precompute")
        else
          vim.defer_fn(wait_ready, 10)
        end
      end

      wait_ready()
    end,
  },
  {
    name = "simulate virtual header wraps long sequences",
    run = function(next)
      with_replay({ string.rep("w", 120), "j" }, {
        "alpha beta gamma delta epsilon zeta",
      }, function()
        local seq1 = get_window(1)
        local lines = header_lines(seq1.buf)
        assert_true(#lines > 4, "expected wrapped sequence lines")
        assert_eq(lines[1], "", "top padding row")
        assert_eq(lines[2], "[1] Local 0/120", "info row (label + local step)")
        assert_eq(lines[3], "Mode NORMAL", "mode row")
        assert_true(lines[4]:sub(1, 8) == "Sequence", "sequence line prefix")
        assert_true(lines[#lines]:find("─", 1, true) ~= nil, "bottom divider row")
      end, next)
    end,
  },
  {
    name = "simulate header uses shared sectionized display",
    run = function(next)
      with_replay({ "3wciwfoo<Esc>2j", "j" }, {
        "alpha beta gamma",
      }, function()
        local seq1 = get_window(1)
        local lines = header_lines(seq1.buf)
        assert_true(lines[4]:find("Sequence 3w", 1, true) ~= nil,
          "first sequence row should show the first section")
        assert_eq(lines[5], "         ciw foo <Esc>", "edit section should align under sequence")
        assert_eq(lines[6], "         2j", "final motion section should align under sequence")
      end, next)
    end,
  },
  {
    name = "simulate replays saved a.json fixture",
    run = function(next)
      local result = load_vimficiency_file("a")
      local user_steps = #sim._debug_tokenize_for_animation(result.user_seq)
      local opt_seq = result.optimal_results[1].seq
      local opt_steps = #sim._debug_tokenize_for_animation(opt_seq)

      with_replay({ result.user_seq, opt_seq }, result.lines, function()
        local final_user = get_snapshot(1, user_steps)
        assert_eq(final_user.lines, result.goal_lines,
          "saved a.json user sequence should reach goal lines")
        assert_eq(final_user.cursor, { result.end_row + 1, result.end_col },
          "saved a.json user sequence should reach goal cursor")

        local final_opt = get_snapshot(2, opt_steps)
        assert_eq(final_opt.lines, result.goal_lines,
          "saved a.json optimal sequence should reach goal lines")
        assert_eq(final_opt.cursor, { result.end_row + 1, result.end_col },
          "saved a.json optimal sequence should reach goal cursor")
      end, next, { start_row = result.start_row, start_col = result.start_col })
    end,
  },
  {
    name = "simulate <CR> focuses the window under cursor and toggles back",
    run = function(next)
      with_replay({ "j", "w", "b" }, {
        "alpha beta gamma",
      }, function()
        -- Pre-condition: three side-by-side windows, same tab.
        local before = sim._debug_get_windows()
        assert_eq(#before, 3, "three replay windows before focus")

        -- Move cursor into window #2, then invoke the toggle handler.
        vim.api.nvim_set_current_win(before[2].win)
        sim._debug_toggle_focus()

        -- Post-condition: a single window remains in the tab; the focused
        -- index is 2 per focus_state.
        local focused = sim._debug_get_windows()
        assert_eq(#focused, 1, "one window after focus")
        assert_eq(focused[1].seq_idx, 2, "focused entry carries seq_idx = 2")
        local state = sim._debug_get_focus_state()
        assert_true(state ~= nil, "focus_state set after focus")
        assert_eq(state.focused_idx, 2, "focused_idx matches the window we were on")

        -- Second `<CR>` from within focus escapes back to the split.
        sim._debug_toggle_focus()
        assert_eq(#sim._debug_get_windows(), 3, "split restored after escape")
        assert_true(sim._debug_get_focus_state() == nil, "focus_state cleared after escape")

        -- seq_idx is populated on every entry post-restore.
        for i, entry in ipairs(sim._debug_get_windows()) do
          assert_eq(entry.seq_idx, i, "restored windows carry seq_idx = " .. i)
        end
      end, next)
    end,
  },
  {
    name = "simulate precompute drains first normal-mode token before snapshot",
    run = function(next)
      local repeats = 3
      local lines = {
        "int main() {",
        "  int m;",
        "  return 0;",
        "}",
      }
      local start_row, start_col = 1, 2  -- 0-indexed: row 2, col 2

      local iter = 1
      local function run_one()
        if iter > repeats then
          next(true)
          return
        end

        with_replay({
          "jf;i<BS>2<Esc><Space>ve",
          "$Ef3r2",
          "$Ef3s<Esc>",
        }, lines, function()

          local states = sim._debug_get_states()
          local function dump_trace(seq_idx)
            local s = states[seq_idx][2]
            local parts = { string.format(
              "      seq%d token=%q final=(%d,%d) mode=%s",
              seq_idx, s.token or "?", s.cursor[1], s.cursor[2], s.mode) }
            for _, p in ipairs(s.trace or {}) do
              parts[#parts + 1] = string.format(
                "      %-18s cursor=(%d,%d) mode=%s",
                p.label, p.cursor[1], p.cursor[2], p.mode)
            end
            return table.concat(parts, "\n")
          end
          assert_true(states[1][2].cursor[1] == 3,
            "iter " .. iter .. ": seq1 first token `j` should advance to row 3\n" ..
            dump_trace(1))
          assert_eq(states[2][2].cursor, { 2, 7 },
            "iter " .. iter .. ": seq2 first token `$` should land at EOL\n" ..
            dump_trace(2))
          assert_eq(states[3][2].cursor, { 2, 7 },
            "iter " .. iter .. ": seq3 first token `$` should land at EOL\n" ..
            dump_trace(3))
        end, function(ok, err)
          if not ok then
            next(false, err)
            return
          end
          iter = iter + 1
          vim.schedule(run_one)
        end, { start_row = start_row, start_col = start_col })
      end

      run_one()
    end,
  },
  {
    name = "simulate <Tab> cycles sim windows (split) and swaps buffer (focus)",
    run = function(next)
      with_replay({ "j", "w", "b" }, {
        "alpha beta gamma",
      }, function()
        local wins = sim._debug_get_windows()
        assert_eq(#wins, 3, "three replay windows before cycling")

        -- Split mode: <Tab> from window 1 → window 2, <Tab> → window 3,
        -- <Tab> wraps back to window 1.
        vim.api.nvim_set_current_win(wins[1].win)
        sim._debug_cycle_next()
        assert_eq(vim.api.nvim_get_current_win(), wins[2].win, "cycle from 1 → 2")
        sim._debug_cycle_next()
        assert_eq(vim.api.nvim_get_current_win(), wins[3].win, "cycle from 2 → 3")
        sim._debug_cycle_next()
        assert_eq(vim.api.nvim_get_current_win(), wins[1].win, "cycle from 3 → 1 (wrap)")

        -- <S-Tab> wraps the other direction.
        sim._debug_cycle_prev()
        assert_eq(vim.api.nvim_get_current_win(), wins[3].win, "cycle prev 1 → 3 (wrap)")

        -- Enter focus on window 3, then <Tab> swaps to sequence 1 in place.
        vim.api.nvim_set_current_win(wins[3].win)
        sim._debug_toggle_focus()
        assert_eq(sim._debug_get_focus_state().focused_idx, 3, "focused on 3")
        sim._debug_cycle_next()
        assert_eq(sim._debug_get_focus_state().focused_idx, 1, "focus wraps 3 → 1")
        local focused = sim._debug_get_windows()
        assert_eq(#focused, 1, "still one window after cycle-in-focus")
        assert_eq(focused[1].seq_idx, 1, "entry's seq_idx follows focused_idx")
      end, next)
    end,
  },
}

local idx = 1

local function finish_all()
  local total = passed + failed
  io.stdout:write(string.format(
    "\n[==========] %d tests from 1 files ran (integration)\n", total))
  io.stdout:write(string.format("[  PASSED  ] %d/%d tests\n", passed, total))
  if failed > 0 then
    io.stderr:write(string.format("[  FAILED  ] %d/%d tests, listed below:\n",
      failed, total))
    for _, ft in ipairs(failed_list) do
      io.stderr:write(string.format("[  FAILED  ] integration :: %s\n", ft.name))
    end
    io.stderr:write(string.format("\n%d FAILED TESTS\n", failed))
  end
  -- Same machine-readable summary `run.sh` looks for on the runner side.
  -- Writes to the env-var'd summary file if present; no-op otherwise.
  if vim.env.VF_TEST_SUMMARY_FILE and vim.env.VF_TEST_SUMMARY_FILE ~= "" then
    local fh = io.open(vim.env.VF_TEST_SUMMARY_FILE, "a")
    if fh then
      fh:write(string.format("passed=%d failed=%d tests=%d\n", passed, failed, total))
      fh:close()
    end
  end
  vim.cmd((failed == 0) and "cquit 0" or "cquit 1")
end

local function run_next()
  if idx > #cases then
    finish_all()
    return
  end

  local case = cases[idx]
  idx = idx + 1
  case.run(function(ok, err)
    if ok then
      pass(case.name)
    else
      fail(case.name, err)
    end
    vim.schedule(run_next)
  end)
end

vim.schedule(run_next)
