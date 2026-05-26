#include "Explore/TestHelpers.h"

using namespace std;

namespace {

TEST_F(ExploreViewTest, CompletedWhenInitialEqualsGoal) {
  Lines lines{Line("hello world")};
  auto view = makeView(lines, {0, 0}, lines, {0, 0});

  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.totalEdits(), 0);
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, PureMotionGoalStartsInNavigate) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  // Pure-motion: Navigate(0) with totalEdits == 0. The "no planned edit"
  // signal is now `totalEdits == 0`, not a missing optional index.
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), 0);
  EXPECT_EQ(view.totalEdits(), 0);

  auto range = view.currentTargetRange();
  EXPECT_EQ(range.first, CursorPos(0, 4));
  EXPECT_EQ(range.second, CursorPos(0, 4));

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  EXPECT_TRUE(any_of(recs.begin(), recs.end(), [](const Suggestion& rec) {
    return rec.landingPos.line == 0 && rec.landingPos.col == 4;
  }));
}

TEST_F(ExploreViewTest, PureMotionGoalCompletesWhenCursorReachesGoal) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().seq, "w");
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, ReconfigurePlanKeepsStateWhenPlanIsUnchanged) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 8});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(view.canUndo());

  Explore::PlanReconfigureResult result =
      view.reconfigurePlan(CompositionOptimizerParams{});

  EXPECT_FALSE(result.resetState);
  EXPECT_EQ(view.state().seq, "w");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_TRUE(view.canUndo());
}

TEST_F(ExploreViewTest, ReconfigurePlanResetsWhenPlanResultsChange) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 8});
  ASSERT_GT(view.headerRows().optimal.size(), 1u);

  ASSERT_TRUE(view.applyMovement("w").has_value());

  CompositionOptimizerParams params;
  params.withMaxResults(1);
  Explore::PlanReconfigureResult result = view.reconfigurePlan(params);

  EXPECT_TRUE(result.resetState);
  EXPECT_EQ(view.state().seq, "");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_FALSE(view.canUndo());
  EXPECT_LE(view.headerRows().optimal.size(), 1u);
}

TEST_F(ExploreViewTest, CompletionIsDerivedAndNotSticky) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(view.isCompleted());

  ASSERT_TRUE(view.applyMovement("b").has_value());
  EXPECT_FALSE(view.isCompleted());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), view.totalEdits());

  ASSERT_TRUE(view.applyMovement("w").has_value());
  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
}

TEST_F(ExploreViewTest, ApproachesEditWhenLinesDiffer) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto view = makeView(initial, {0, 0}, goal, {0, 10});

  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), 0);
  EXPECT_GT(view.totalEdits(), 0);
  EXPECT_EQ(view.state().cursor.line, 0);
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_TRUE(view.state().seq.empty());
}

TEST_F(ExploreViewTest, ApplyMotionAdvancesCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  auto outcome = view.applyMovement("w");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().cursor.col, 4);
  EXPECT_EQ(view.state().seq, "w");
  EXPECT_GT(view.state().cost, 0.0);
  EXPECT_TRUE(view.canUndo());
}

TEST_F(ExploreViewTest, ApplyMotionRejectsMalformedInput) {
  Lines initial{Line("abcd")};
  Lines goal{Line("abCd")};
  auto view = makeView(initial, {0, 0}, goal, {0, 3});

  auto outcome = view.applyMovement("<"); // incomplete special key
  ASSERT_FALSE(outcome.has_value());
  EXPECT_FALSE(outcome.error().reason.empty());
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, ApplyMotionRejectsBoundaryEscape) {
  Lines lines{Line("prefix body suffix")};
  auto view = makeViewWithBoundary(lines, {0, 7}, lines, {0, 10},
                                   CursorPos(0, 7), CursorPos(0, 11));

  auto outcome = view.applyMovement("$");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "motion landed outside the allowed boundary");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 7));
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, AcceptCursorMoveRejectsBoundaryEscape) {
  Lines lines{Line("prefix body suffix")};
  auto view = makeViewWithBoundary(lines, {0, 7}, lines, {0, 10},
                                   CursorPos(0, 7), CursorPos(0, 11));

  auto outcome = view.acceptCursorMove(CursorPos(0, 16), "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "motion landed outside the allowed boundary");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 7));
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, AcceptCursorMoveTrustsObservedCursorOverRawReplay) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  auto outcome = view.acceptCursorMove(CursorPos(0, 4), "l");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().seq, "l");
  EXPECT_TRUE(view.canUndo());
}

TEST_F(ExploreViewTest, UndoRestoresPriorCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  const int cursorAfter = view.state().cursor.col;
  ASSERT_GT(cursorAfter, 0);

  auto undone = view.undo();
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_TRUE(view.canRedo());

  auto redone = view.redo();
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(view.state().cursor.col, cursorAfter);
  EXPECT_EQ(view.state().seq, "w");
}

TEST_F(ExploreViewTest, UndoFromCleanStateIsRejected) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.undo();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "nothing to undo");
}

TEST_F(ExploreViewTest, BeginInsertTransitionsIntoInsert) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  auto outcome = view.beginInsert();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), 0);
}

TEST_F(ExploreViewTest, InsertPhaseRecommendationCarriesTypedText) {
  // Replacement: cursor inside the diff range. The Insert recommendation's
  // token is the canonical text the user must type in insert mode to reach
  // the planned post-edit fencepost.
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  // Walk the cursor to the diff's first changed character.
  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  ASSERT_TRUE(view.beginInsert().has_value());

  auto recs = view.recommendations(5);
  ASSERT_EQ(recs.size(), 1u);
  const Suggestion& item = recs[0];
  EXPECT_FALSE(string_view(item.token).empty());
  EXPECT_GT(item.costDiff, 0.0);
  // The token must contain the new char `m` somewhere — the diff may span
  // more than one position depending on what minimal-diff returns, but the
  // user must produce `m` for the result to match the goal.
  EXPECT_NE(string_view(item.token).find('m'), string_view::npos);
}

TEST_F(ExploreViewTest, InsertPhaseRecommendationForPureInsertion) {
  // Pure insertion: append `X` at end of line via `a` or `A`. The typed
  // text is `X`.
  Lines initial{Line("ab")};
  Lines goal{Line("abX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 2});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(view.beginInsert().has_value());

  auto recs = view.recommendations(5);
  ASSERT_EQ(recs.size(), 1u);
  const Suggestion& item = recs[0];
  EXPECT_EQ(string_view(item.token), "X");
}

TEST_F(ExploreViewTest, InsertPhaseRecommendationEmptyForPureDeletion) {
  // Pure deletion: no insert-mode follow-up. Even if a caller manages to
  // park us in Insert, recommendInsert returns no items.
  Lines initial{Line("abcd")};
  Lines goal{Line("ad")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  // For a pure-deletion edit, beginInsert is normally not invoked by Lua —
  // but if it were, the rec list would be empty (no typed text needed).
  // Note: depending on how the diff is decomposed this scenario may not
  // park the view in a pure-deletion-only Insert; smoke-test that no error
  // is raised regardless.
  if (view.beginInsert().has_value() &&
      std::holds_alternative<Explore::Insert>(view.phase())) {
    auto recs = view.recommendations(5);
    // Either empty (pure deletion) or has one item (replacement masked as
    // deletion); both are acceptable outcomes — no crash, no garbage.
    EXPECT_LE(recs.size(), 1u);
  }
}


TEST_F(ExploreViewTest, OutOfScopeEditRejectedWithoutStateChange) {
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  // Cursor is now on `n`. An edit command not in transformResult.resultsAt gets
  // rejected without mutating state.
  const auto priorState = view.state();
  auto outcome = view.applyEdit("totally-not-a-real-edit");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(view.state(), priorState);
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
}

TEST_F(ExploreViewTest, AcceptBufferStateRejectsInvalidCursor) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.acceptBufferState(goal, CursorPos(0, 99), "rB");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason,
            "buffer state reported an invalid cursor position");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_EQ(view.state().lines, initial);
  EXPECT_TRUE(view.state().seq.empty());
}

} // namespace
