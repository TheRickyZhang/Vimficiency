return function(ctx)
  local sim = ctx.sim
  local highlights = ctx.highlights
  local with_replay = ctx.with_replay
  local get_window = ctx.get_window
  local header_lines = ctx.header_lines
  local header_has_chunk = ctx.header_has_chunk
  local assert_eq = ctx.assert_eq
  local assert_true = ctx.assert_true

  return {
  {
    name = "simulate header keeps typed replay chunks visually contiguous",
    run = function(next)
      with_replay({ "A2<Space>*<Space>i<Esc>" }, { "cout << endl;" }, function()
        local seq1 = get_window(1)
        local lines = header_lines(seq1.buf)
        assert_eq(lines[4], "Sequence A 2␣*␣i <Esc>",
          "typed replay chunks should render as one contiguous typed section")
      end, next)
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
    name = "simulate header highlights current token",
    run = function(next)
      with_replay({ "3wciwfoo<Esc>2j" }, {
        "alpha beta gamma",
      }, function()
        local seq1 = get_window(1)
        sim._debug_seek_to(2)
        assert_true(header_has_chunk(seq1.buf, "ciw", highlights.REPLAY_CURRENT),
          "current command token should be highlighted")
        assert_true(not header_has_chunk(seq1.buf, "3w", highlights.REPLAY_CURRENT),
          "previous token should not be highlighted")

        sim._debug_seek_to(3)
        assert_true(header_has_chunk(seq1.buf, "foo", highlights.REPLAY_CURRENT),
          "current typed token should be highlighted")
      end, next)
    end,
  },
  {
    name = "simulate header does not highlight completed shorter sequence",
    run = function(next)
      with_replay({ "j", "jj" }, {
        "one",
        "two",
        "three",
      }, function()
        local seq1 = get_window(1)
        local seq2 = get_window(2)
        sim._debug_seek_to(2)
        assert_true(not header_has_chunk(seq1.buf, "j", highlights.REPLAY_CURRENT),
          "completed shorter sequence should not keep highlighting its last token")
        assert_true(header_has_chunk(seq2.buf, "j", highlights.REPLAY_CURRENT),
          "longer sequence should highlight the active token")
      end, next)
    end,
  },
  }
end
