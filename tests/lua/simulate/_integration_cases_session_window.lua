return function(ctx)
  local helpers = ctx.helpers
  local sim = ctx.sim
  local session = ctx.session
  local session_store = ctx.session_store
  local with_replay = ctx.with_replay
  local assert_eq = ctx.assert_eq
  local assert_true = ctx.assert_true

  return {
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
            local user = assert(sim._debug_get_pool().user, "missing user replay pool entry")
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
end
