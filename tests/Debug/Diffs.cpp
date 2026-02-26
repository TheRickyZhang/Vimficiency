/*
TEST_F(DebugTest, DISABLED_InvestigatePureInsertionDiffs) {
  cerr << "=== Pure Insertion Diff Investigation ===" << endl;

  auto printDiff = [](const char* name, const Lines& initial, const Lines& goal) {
    cerr << endl << name << ":" << endl;
    cerr << "  Initial: " << initial << endl;
    cerr << "  Goal:    " << goal << endl;

    auto diffs = Myers::calculate(initial, goal);
    cerr << "  Diffs: " << diffs.size() << endl;
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      cerr << "    [" << i << "] deleted='" << d.deletedText
           << "' inserted='" << d.insertedText << "'" << endl;
      cerr << "        beginPos=(" << d.beginPos.line << "," << d.beginPos.col
           << ") endPos=(" << d.endPos.line << "," << d.endPos.col << ")" << endl;
      cerr << "        isPureInsertion=" << d.isPureInsertion()
           << " insertedText.back()=";
      if (!d.insertedText.empty()) {
        char c = d.insertedText.back();
        cerr << (c == '\n' ? "'\\n'" : string(1, c));
      } else {
        cerr << "(empty)";
      }
      cerr << endl;
    }
  };

  // Case 1: Insert new line between existing lines
  // a       a
  // c  ->   b
  //         c
  printDiff("Case 1: Insert new line 'b' between 'a' and 'c'",
            {"a", "c"}, {"a", "b", "c"});

  // Case 2: Append to end of line
  // a       ab
  // c  ->   c
  printDiff("Case 2: Append 'b' to end of line 'a'",
            {"a", "c"}, {"ab", "c"});

  // Case 3: Append 'b' and newline (creating empty line)
  // a       ab
  // c  ->   (empty)
  //         c
  printDiff("Case 3: Append 'b\\n' creating new empty line",
            {"a", "c"}, {"ab", "", "c"});

  // Case 4: Insert at beginning of line
  // a       ba
  // c  ->   c
  printDiff("Case 4: Insert 'b' at start of line 'a'",
            {"a", "c"}, {"ba", "c"});

  // Case 5: Insert in middle of line
  // abc     axbc
  // d  ->   d
  printDiff("Case 5: Insert 'x' in middle of 'abc'",
            {"abc", "d"}, {"axbc", "d"});
}

TEST_F(DebugTest, CompositionDiffMerging) {
  cerr << "=== CompositionSearchContext Diff Merging ===" << endl;

  Config config = Config::uniform();
  CompositionOptimizerParams params;

  auto testMerge = [&](const char* name, const Lines& initial, const Lines& goal,
                       int expectedDiffs) {
    cerr << endl << name << ":" << endl;
    cerr << "  Initial: " << initial << endl;
    cerr << "  Goal:    " << goal << endl;

    // Create context to trigger merge
    CompositionSearchContext ctx(initial, CursorPos(0, 0), goal, "",
        NavContext(), MotionBoundary(), params, config);

    cerr << "  Merged diffs: " << ctx.totalEdits() << endl;
    for (int i = 0; i < ctx.totalEdits(); i++) {
      const auto& d = ctx.edits[i].diffState;
      cerr << "    [" << i << "] deleted='" << d.deletedText
           << "' inserted='" << d.insertedText << "'" << endl;
      cerr << "        beginPos=(" << d.beginPos.line << "," << d.beginPos.col
           << ") isPureInsertion=" << d.isPureInsertion() << endl;
    }

    EXPECT_EQ(ctx.totalEdits(), expectedDiffs) << "Expected " << expectedDiffs
        << " diff(s) for: " << name;
  };

  // Case 1: Insert new line (already 1 diff, no merge needed)
  testMerge("Insert new line 'b' between 'a' and 'c'",
            {"a", "c"}, {"a", "b", "c"}, 1);

  // Case 2: Append to end of line (1 diff, no merge)
  testMerge("Append 'b' to end of line 'a'",
            {"a", "c"}, {"ab", "c"}, 1);

  // Case 3: Should merge: insert 'b' at (0,1) + insert '\n' at (1,0) → insert 'b\n' at (0,1)
  testMerge("Append 'b\\n' creating new empty line (should merge)",
            {"a", "c"}, {"ab", "", "c"}, 1);

  // Case 4: Insert at start of line (1 diff)
  testMerge("Insert 'b' at start of line 'a'",
            {"a", "c"}, {"ba", "c"}, 1);

  // Also test the optimizer for Case 3
  cerr << endl << "=== Case 3 Optimizer Test ===" << endl;
  {
    Lines initial = {"a", "c"};
    Lines goal = {"ab", "", "c"};
    CursorPos initialPos(0, 0);

    CompositionOptimizer opt(config);
    auto compResult = opt.optimize(
        initial, initialPos, goal, CursorPos(0, 0), params);
    const auto& results = compResult.results;

    cerr << "  Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      cerr << "    [" << i << "] " << results[i].getSequenceString()
           << " cost=" << results[i].keyCost << endl;
    }
  }
}


*/
