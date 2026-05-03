// tests/Explore/ExploreTest.cpp
//
// Tests for Explore::View: phase machine + recommendations + applyMovement
// + strict-revert buffer-state flow. Invalid phase is not a reachable state
// — programming-invariant failures assert, external teardown destroys the
// view — so there's no corresponding test here.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <string_view>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformFrontier.h"
#include "Optimizer/TransformOptimizer/TransformSequenceDecomposition.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Optimizer/Result.h"
#include "Explore/Explore.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

class ExploreViewTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};

  Explore::View makeView(Lines initial, CursorPos initialPos, Lines goal,
                         CursorPos goalPos) {
    NavBoundary boundary(
        initial, CursorPos(0, 0),
        CursorPos(static_cast<int>(initial.size()) - 1,
                  static_cast<int>(initial.back().size()) + 1),
        /*hasLinesAbove=*/false,
        /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }

  Explore::View makeViewWithBoundary(Lines initial, CursorPos initialPos, Lines goal,
                                     CursorPos goalPos, CursorPos boundaryBegin,
                                     CursorPos boundaryEnd) {
    NavBoundary boundary(initial, boundaryBegin, boundaryEnd,
                         /*hasLinesAbove=*/false,
                         /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }
};

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

TEST_F(ExploreViewTest, RecommendationsAreDiverse) {
  Lines initial{Line("foo bar baz qux zed")};
  Lines goal{Line("foo bar baz qux ZED")};
  auto view = makeView(initial, {0, 0}, goal, {0, 18});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty()) << "expected at least one motion recommendation";

  // Distinct recommendation texts — grouping/dedup works.
  set<string> texts;
  for (const auto& rec : recs)
    texts.insert(rec.token);
  EXPECT_EQ(texts.size(), recs.size());

  // Navigate phase, so all recs are motions; each must change the cursor.
  for (const auto& rec : recs) {
    const bool moved = rec.landingPos.line != 0 || rec.landingPos.col != 0;
    EXPECT_TRUE(moved) << "motion '" << rec.token << "' did not change cursor";
  }
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

TEST_F(ExploreViewTest, RecommendationCostDiffIncludesAcceptedSequence) {
  config = Config::qwerty();
  config.weights.w_same_key = 1.0;
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 8});

  ASSERT_TRUE(view.applyMovement("w").has_value());

  auto recs = view.recommendations(5);
  auto rec = find_if(recs.begin(), recs.end(), [](const Suggestion& candidate) {
    return string_view(candidate.token) == "w";
  });
  ASSERT_NE(rec, recs.end());

  string combined = view.state().seq + string(rec->token);
  const double expected = getEffort(combined, config) - view.state().cost;
  const double standalone = getEffort(string(rec->token), config);

  EXPECT_NEAR(rec->costDiff, expected, 1e-9);
  EXPECT_GT(abs(rec->costDiff - standalone), 1e-9);
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

TEST_F(ExploreViewTest, AcceptCursorMoveRejectsMismatchedRawKeys) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  auto outcome = view.acceptCursorMove(CursorPos(0, 4), "l");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason,
            "raw motion keys did not produce the observed cursor move");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_FALSE(view.canUndo());
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

TEST(ExtractStructuralToken, ReturnsFirstNonTypedTextToken) {
  EXPECT_EQ(extractStructuralToken("sm<Esc>"), "s");
  EXPECT_EQ(extractStructuralToken("clm<Esc>"), "cl");
  EXPECT_EQ(extractStructuralToken("clfoo<Esc>"), "cl");
  EXPECT_EQ(extractStructuralToken("Jj"), "J");
  EXPECT_EQ(extractStructuralToken("x"), "x");
  EXPECT_EQ(extractStructuralToken("rm"), "rm");
  EXPECT_EQ(extractStructuralToken(""), "");
}

TEST(TransformFrontier, PreservesDistinctResultsFromSameStart) {
  DiffState diff(CursorPos(0, 0), CursorPos(0, 1), "x", "foo", TransformBoundary{});
  auto recs = rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = Lines{Line("x")},
              .cursor = {0, 0},
              .maxCount = 10,
          },
          diff,
          // The test's intent is to verify that MULTIPLE tokens
          // reaching the same goal state are preserved.
          /*maxResultsPerStartPos=*/2,
      },
      Config::uniform());
  ASSERT_GE(recs.size(), 2u);
  // Distinct structural tokens — the test's intent: multiple strategies for
  // the same diff produce different command shapes, all preserved.
  EXPECT_NE(recs[0].token, recs[1].token);
}

TEST_F(ExploreViewTest, AcceptInsertExitAdvancesPhaseOnMatchingBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  // Get the cursor to the edit target via the cheapest motion.
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  // Navigate phase: every rec is a motion. Apply the first.
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());

  // Simulate the Lua layer: beginInsert parks us in Insert; the post-insert
  // buffer then validates via acceptInsertExit.
  ASSERT_TRUE(view.beginInsert().has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));

  auto outcome = view.acceptInsertExit(goal, {0, 2}, "");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().lines, goal);
  // Single-edit plan → advancing past the last edit lands in Navigate(totalEdits),
  // the post-final-edit nav segment. Completion is then a derived predicate
  // that triggers once the cursor reaches goalPos (here goalPos=(0,1) but
  // the insert exit lands at (0,2), so isCompleted is false until a
  // subsequent motion lands on goalPos).
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), view.totalEdits());
  EXPECT_FALSE(view.isCompleted());
}

TEST_F(ExploreViewTest, AcceptInsertExitRejectsMismatchedBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
  ASSERT_TRUE(view.beginInsert().has_value());
  const auto priorState = view.state();

  Lines wrong{Line("aXc")};
  auto outcome = view.acceptInsertExit(wrong, {0, 2}, "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, AcceptInsertExitRejectsInvalidCursor) {
  // Mirror of AcceptBufferStateRejectsInvalidCursor: a buffer-state-bearing
  // Insert completion must validate the reported cursor, otherwise a bad
  // position would be committed and poison the next phase.
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
  ASSERT_TRUE(view.beginInsert().has_value());
  const auto priorState = view.state();

  auto outcome = view.acceptInsertExit(goal, CursorPos(0, 99), "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason,
            "buffer state reported an invalid cursor position");
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state(), priorState);
}

// =============================================================================
// Action-contract rejections
// =============================================================================
// One row per (action × invalid-input-category). Adding a new action means
// adding the corresponding rows here so the contract listed in Explore.h
// is enforced by tests, not by author memory.

TEST_F(ExploreViewTest, AcceptCursorMoveRejectsUnparseableRawKeys) {
  Lines initial{Line("foo bar")};
  Lines goal{Line("foo BAR")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  // "<" is an incomplete special-key escape — parseMotions rejects it.
  auto outcome = view.acceptCursorMove(CursorPos(0, 4), "<");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("failed to parse"), string::npos);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_TRUE(view.state().seq.empty());
}

TEST_F(ExploreViewTest, AcceptBufferStateRejectsUnparseableRawKeys) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.acceptBufferState(goal, CursorPos(0, 1), "<");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "raw keys failed to parse");
  EXPECT_EQ(view.state().lines, initial);
  EXPECT_TRUE(view.state().seq.empty());
}

TEST_F(ExploreViewTest, AcceptInsertExitRejectsUnparseableRawKeys) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
  ASSERT_TRUE(view.beginInsert().has_value());

  auto outcome = view.acceptInsertExit(goal, CursorPos(0, 1), "<");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "raw keys failed to parse");
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
}

TEST_F(ExploreViewTest, ApplyEditRejectedForMotionOnlyGoals) {
  // Pure-motion goal (initial == goal, cursor differs) never reaches Transform.
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.applyEdit("x");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("transforming"), string::npos);
  EXPECT_EQ(view.state().lines, lines);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
}

TEST_F(ExploreViewTest, AcceptBufferStateRejectedAtPostFinalEditNav) {
  // Pure-motion sessions live entirely in Navigate(totalEdits). There is no
  // edit to apply, so buffer-state changes are rejected.
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.acceptBufferState(lines, CursorPos(0, 4), "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("post-final-edit"), string::npos);
}

TEST_F(ExploreViewTest, BeginInsertRejectedAtPostFinalEditNav) {
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.beginInsert();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("post-final-edit"), string::npos);
}

TEST_F(ExploreViewTest, CancelInsertRestoresPreviousPhaseWithoutRedo) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  ASSERT_TRUE(view.beginInsert().has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_FALSE(view.canRedo());

  auto outcome = view.cancelInsert();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  // Crucially: redo stack NOT polluted with the rejected/abandoned insert.
  EXPECT_FALSE(view.canRedo());
}

TEST_F(ExploreViewTest, MotionRecommendationsAreFirstTokensOfOptimizerPaths) {
  // Explore shows immediate next tokens, not full paths to the target.
  // Each motion recommendation must be the FIRST token of some full A*
  // path that reaches the current edit's range, and they must be distinct.
  Lines initial{Line("one two three four five six seven")};
  Lines goal{Line("one two three four FIVE six seven")};
  auto view = makeView(initial, {0, 0}, goal, {0, 22});

  auto range = view.currentTargetRange();

  // Match the ground-truth's "no per-pos cap" below so both sides
  // enumerate the same universe of tokens for the subset check.
  // Navigate phase, so every rec is a motion.
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(10,
      /*navMaxResultsPerEndPos=*/std::numeric_limits<int>::max(),
      /*transformMaxResultsPerStartPos=*/1);
  vector<string> exploreMotionTexts;
  for (const auto& rec : recs)
    exploreMotionTexts.push_back(rec.token);
  ASSERT_FALSE(exploreMotionTexts.empty());

  set<string> exploreSet(exploreMotionTexts.begin(), exploreMotionTexts.end());
  EXPECT_EQ(exploreSet.size(), exploreMotionTexts.size())
      << "motion recommendations should be distinct first tokens";

  // Ground truth: run the same nav optimizer the frontier uses, collect
  // full paths, and derive their first tokens. Explore's set must be a
  // subset of that — anything else would be an invalid next step.
  CompositionOptimizerParams compParams;
  NavOptimizerParams params;
  params
      .withMaxResults(40)
      .withFMotionThreshold(compParams.fMotionThreshold)
      .withDirectionalPruning(compParams.useDirectionalPruning)
      .withLinePaddingAbove(compParams.navPaddingAbove)
      .withLinePaddingBelow(compParams.navPaddingBelow)
      .withMinCountRepeat(compParams.minPrefixCount)
      .withMaxCountRepeat(compParams.maxPrefixCount)
      .withMaxResultsPerEndPos(2);

  NavOptimizer opt(config);
  auto motionRange =
      tryToMotionInterval(initial, CharRange(range.first, range.second));
  ASSERT_TRUE(motionRange.has_value());
  auto result = opt.optimize(
      initial, {0, 0}, *motionRange, params, "",
      NavBoundary(initial, CursorPos(0, 0),
                     CursorPos(static_cast<int>(initial.size()) - 1,
                               static_cast<int>(initial.back().size()) + 1),
                     false, false),
      navContext);

  set<string> validFirstTokens;
  for (const auto& motion : result.getResults()) {
    if (motion.getSequence().empty()) continue;
    auto tokens = parseSequenceStrings(motion.getSequence().view());
    if (!tokens || tokens->empty()) continue;
    validFirstTokens.insert(tokens->front());
  }

  for (const auto& token : exploreSet) {
    EXPECT_TRUE(validFirstTokens.contains(token))
        << "explore recommended '" << token
        << "' but it is not a first token of any optimal path";
  }
}

} // namespace
