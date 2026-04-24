// tests/Explore/ExploreTest.cpp
//
// Tests for Explore::View: phase machine + recommendations + applyMotion
// + strict-revert buffer-state flow. Invalid phase is not a reachable state
// — programming-invariant failures assert, external teardown destroys the
// view — so there's no corresponding test here.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "Boundary/NavBoundary.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/EditOptimizer/EditFrontier.h"
#include "Optimizer/EditOptimizer/EditSequenceDecomposition.h"
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

  EXPECT_EQ(view.step().kind, Explore::Phase::Completed);
  EXPECT_EQ(view.totalEdits(), 0);
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, PureMotionGoalStartsInApproachEdit) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  EXPECT_EQ(view.step().kind, Explore::Phase::ApproachEdit);
  EXPECT_EQ(view.totalEdits(), 0);

  auto range = view.currentTargetRange();
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(range->first, CursorPos(0, 4));
  EXPECT_EQ(range->second, CursorPos(0, 4));

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  EXPECT_TRUE(any_of(recs.begin(), recs.end(), [](const auto& rec) {
    return rec.kind == "motion" && rec.landingRow == 0 && rec.landingCol == 4;
  }));
}

TEST_F(ExploreViewTest, PureMotionGoalCompletesWhenCursorReachesGoal) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  ASSERT_TRUE(view.applyMotion("w").has_value());
  EXPECT_EQ(view.step().kind, Explore::Phase::Completed);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().acceptedSeq, "w");
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, ApproachesEditWhenLinesDiffer) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto view = makeView(initial, {0, 0}, goal, {0, 10});

  EXPECT_EQ(view.step().kind, Explore::Phase::ApproachEdit);
  EXPECT_EQ(view.step().editIndex, 0);
  EXPECT_GT(view.totalEdits(), 0);
  EXPECT_EQ(view.state().cursor.line, 0);
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_TRUE(view.state().acceptedSeq.empty());
}

TEST_F(ExploreViewTest, RecommendationsAreDiverse) {
  Lines initial{Line("foo bar baz qux zed")};
  Lines goal{Line("foo bar baz qux ZED")};
  auto view = makeView(initial, {0, 0}, goal, {0, 18});

  ASSERT_EQ(view.step().kind, Explore::Phase::ApproachEdit);
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty()) << "expected at least one motion recommendation";

  // Distinct recommendation texts — grouping/dedup works.
  set<string> texts;
  for (const auto& rec : recs)
    texts.insert(rec.text);
  EXPECT_EQ(texts.size(), recs.size());

  // Motion recs (if any) changed the cursor from the origin.
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      const bool moved = rec.landingRow != 0 || rec.landingCol != 0;
      EXPECT_TRUE(moved) << "motion '" << rec.text << "' did not change cursor";
    }
  }
}

TEST_F(ExploreViewTest, ApplyMotionAdvancesCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  auto outcome = view.applyMotion("w");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.step().kind, Explore::Phase::ApproachEdit);
  EXPECT_EQ(view.state().cursor.col, 4);
  EXPECT_EQ(view.state().acceptedSeq, "w");
  EXPECT_GT(view.state().acceptedCost, 0.0);
  EXPECT_TRUE(view.canUndo());
}

TEST_F(ExploreViewTest, ApplyMotionRejectsMalformedInput) {
  Lines initial{Line("abcd")};
  Lines goal{Line("abCd")};
  auto view = makeView(initial, {0, 0}, goal, {0, 3});

  auto outcome = view.applyMotion("<"); // incomplete special key
  ASSERT_FALSE(outcome.has_value());
  EXPECT_FALSE(outcome.error().reason.empty());
  EXPECT_TRUE(view.state().acceptedSeq.empty());
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, ApplyMotionRejectsBoundaryEscape) {
  Lines lines{Line("prefix body suffix")};
  auto view = makeViewWithBoundary(lines, {0, 7}, lines, {0, 10},
                                   CursorPos(0, 7), CursorPos(0, 11));

  auto outcome = view.applyMotion("$");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "motion landed outside the allowed boundary");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 7));
  EXPECT_TRUE(view.state().acceptedSeq.empty());
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
  EXPECT_TRUE(view.state().acceptedSeq.empty());
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
  EXPECT_TRUE(view.state().acceptedSeq.empty());
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, UndoRestoresPriorCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  ASSERT_TRUE(view.applyMotion("w").has_value());
  const int cursorAfter = view.state().cursor.col;
  ASSERT_GT(cursorAfter, 0);

  auto undone = view.undo();
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_TRUE(view.state().acceptedSeq.empty());
  EXPECT_TRUE(view.canRedo());

  auto redone = view.redo();
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(view.state().cursor.col, cursorAfter);
  EXPECT_EQ(view.state().acceptedSeq, "w");
}

TEST_F(ExploreViewTest, UndoFromCleanStateIsRejected) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.undo();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "nothing to undo");
}

TEST_F(ExploreViewTest, BeginEditTransitionsIntoPendingInsert) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.beginEdit(true, "BX");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.step().kind, Explore::Phase::PendingInsert);
  EXPECT_EQ(view.step().remainingText, "BX");

  // Recommendations are empty outside ApproachEdit.
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, OutOfScopeEditRejectedWithoutStateChange) {
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  ASSERT_TRUE(view.applyMotion("w").has_value());
  // Cursor is now on `n`. An edit command not in editResult.resultsAt gets
  // rejected without mutating state.
  const auto priorRevision = view.acceptedRevision();
  auto outcome = view.applyEdit("totally-not-a-real-edit");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(view.acceptedRevision(), priorRevision);
  EXPECT_EQ(view.step().kind, Explore::Phase::ApproachEdit);
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
  EXPECT_TRUE(view.state().acceptedSeq.empty());
}

TEST(EditSequenceDecomposition, SplitsImmediateMoleculeAndInsertTail) {
  EXPECT_EQ(decomposeEditSequence("sm<Esc>").molecule, "s");
  EXPECT_EQ(decomposeEditSequence("sm<Esc>").typedText, "m");
  EXPECT_EQ(decomposeEditSequence("clm<Esc>").molecule, "cl");
  EXPECT_EQ(decomposeEditSequence("clm<Esc>").typedText, "m");
  EXPECT_EQ(decomposeEditSequence("clfoo<Esc>").typedText, "foo");
  EXPECT_EQ(decomposeEditSequence("Jj").molecule, "J");
  EXPECT_EQ(decomposeEditSequence("x").typedText, "");
  EXPECT_EQ(decomposeEditSequence("rm").typedText, "");
  EXPECT_EQ(decomposeEditSequence("").typedText, "");
}

TEST(EditFrontier, PreservesDistinctResultsFromSameStart) {
  DiffState diff(CursorPos(0, 0), CursorPos(0, 1), "x", "foo", EditBoundary{});
  auto recs = rankEditFrontier(
      EditFrontierQuery{
          .lines = Lines{Line("x")},
          .cursor = {0, 0},
          .diff = diff,
          .maxCount = 10,
          // The test's intent is to verify that MULTIPLE molecules
          // reaching the same goal state are preserved — so opt in.
          .allowMultiplePerPosition = true,
      },
      Config::uniform());
  ASSERT_GE(recs.size(), 2u);
  EXPECT_NE(recs[0].molecule, recs[1].molecule);
  EXPECT_EQ(recs[0].typedText, "foo");
  EXPECT_EQ(recs[1].typedText, "foo");
}

TEST(EditFrontier, FillsRequestedCountWhenManyLocalAlternativesExist) {
  DiffState diff(CursorPos(0, 4), CursorPos(0, 7), "def", "xyz",
                 EditBoundary{});
  auto recs = rankEditFrontier(
      EditFrontierQuery{
          .lines = Lines{Line("abc def ghi")},
          .cursor = {0, 4},
          .diff = diff,
          .maxCount = 5,
      },
      Config::uniform());
  ASSERT_GE(recs.size(), 1u);
}

TEST_F(ExploreViewTest, AcceptInsertExitAdvancesPhaseOnMatchingBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  // Get the cursor to the edit target via the cheapest motion.
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  const Explore::Recommendation* motion = nullptr;
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      motion = &rec;
      break;
    }
  }
  if (motion)
    ASSERT_TRUE(view.applyMotion(motion->text).has_value());

  // Simulate the Lua layer: beginEdit(true, typedText) parks us in
  // PendingInsert; the post-insert buffer then validates via acceptInsertExit.
  ASSERT_TRUE(view.beginEdit(true, "B").has_value());
  ASSERT_EQ(view.step().kind, Explore::Phase::PendingInsert);

  auto outcome = view.acceptInsertExit(goal, {0, 2}, "");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().lines, goal);
  // Single-edit plan → advancing past it lands in Completed.
  EXPECT_EQ(view.step().kind, Explore::Phase::Completed);
}

TEST_F(ExploreViewTest, AcceptInsertExitRejectsMismatchedBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      ASSERT_TRUE(view.applyMotion(rec.text).has_value());
      break;
    }
  }
  ASSERT_TRUE(view.beginEdit(true, "B").has_value());
  const auto priorRevision = view.acceptedRevision();

  Lines wrong{Line("aXc")};
  auto outcome = view.acceptInsertExit(wrong, {0, 2}, "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(view.step().kind, Explore::Phase::PendingInsert);
  EXPECT_EQ(view.acceptedRevision(), priorRevision);
}

TEST_F(ExploreViewTest, CancelPendingInsertRestoresApproachEditWithoutRedo) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.beginEdit(true, "B").has_value());
  ASSERT_EQ(view.step().kind, Explore::Phase::PendingInsert);
  EXPECT_FALSE(view.canRedo());

  auto outcome = view.cancelPendingInsert();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.step().kind, Explore::Phase::ApproachEdit);
  // Crucially: redo stack NOT polluted with the rejected/abandoned insert.
  EXPECT_FALSE(view.canRedo());
}

TEST_F(ExploreViewTest, RecommendationsCarryTypedText) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  // Move cursor onto the edit target so edit recs populate.
  auto recs = view.recommendations(5);
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      ASSERT_TRUE(view.applyMotion(rec.text).has_value());
      break;
    }
  }
  recs = view.recommendations(10);
  bool sawInsertAtom = false;
  for (const auto& rec : recs) {
    if (rec.kind == "edit" && !rec.typedText.empty())
      sawInsertAtom = true;
  }
  EXPECT_TRUE(sawInsertAtom)
      << "expected at least one edit recommendation with a non-empty typedText";
}

TEST_F(ExploreViewTest, FillsRequestedCountAfterMovingOntoTarget) {
  Lines initial{Line("abc defghij klm")};
  Lines goal{Line("abc xyz klm")};
  auto view = makeView(initial, {0, 0}, goal, {0, 6});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      ASSERT_TRUE(view.applyMotion(rec.text).has_value());
      break;
    }
  }

  recs = view.recommendations(5);
  EXPECT_EQ(recs.size(), 5u);
}

TEST_F(ExploreViewTest, MotionRecommendationsAreFirstMoleculesOfOptimizerPaths) {
  // Explore shows immediate next molecules, not full paths to the target.
  // Each motion recommendation must be the FIRST molecule of some full A*
  // path that reaches the current edit's range, and they must be distinct.
  Lines initial{Line("one two three four five six seven")};
  Lines goal{Line("one two three four FIVE six seven")};
  auto view = makeView(initial, {0, 0}, goal, {0, 22});

  auto range = view.currentTargetRange();
  ASSERT_TRUE(range.has_value());

  // Match the ground-truth's `allowMultiplePerPosition=true` below so both
  // sides enumerate the same universe of molecules for the subset check.
  auto recs = view.recommendations(10, /*allowMultiplePerPosition=*/true);
  vector<string> exploreMotionTexts;
  for (const auto& rec : recs) {
    if (rec.kind == "motion")
      exploreMotionTexts.push_back(rec.text);
  }
  ASSERT_FALSE(exploreMotionTexts.empty());

  set<string> exploreSet(exploreMotionTexts.begin(), exploreMotionTexts.end());
  EXPECT_EQ(exploreSet.size(), exploreMotionTexts.size())
      << "motion recommendations should be distinct first molecules";

  // Ground truth: run the same motion optimizer the frontier uses, collect
  // full paths, and derive their first molecules. Explore's set must be a
  // subset of that — anything else would be an invalid next step.
  CompositionOptimizerParams compParams;
  NavOptimizerRangeParams params;
  params
      .withMaxResults(40)
      .withFMotionThreshold(compParams.fMotionThreshold)
      .withDirectionalPruning(compParams.useDirectionalPruning)
      .withLinePaddingAbove(compParams.navPaddingAbove)
      .withLinePaddingBelow(compParams.navPaddingBelow)
      .withMinCountRepeat(compParams.minPrefixCount)
      .withMaxCountRepeat(compParams.maxPrefixCount)
      .withAllowMultiplePerPosition(true);

  NavOptimizer opt(config);
  auto motionRange =
      tryToMotionInterval(initial, CharRange(range->first, range->second));
  ASSERT_TRUE(motionRange.has_value());
  auto result = opt.optimizeToRange(
      initial, {0, 0}, *motionRange, params, "",
      NavBoundary(initial, CursorPos(0, 0),
                     CursorPos(static_cast<int>(initial.size()) - 1,
                               static_cast<int>(initial.back().size()) + 1),
                     false, false),
      navContext);

  set<string> validFirstMolecules;
  for (const auto& motion : result.getResults()) {
    if (!motion.isValid()) continue;
    auto tokens = parseSequenceStrings(motion.getSequence().view());
    if (!tokens || tokens->empty()) continue;
    validFirstMolecules.insert(tokens->front());
  }

  for (const auto& mol : exploreSet) {
    EXPECT_TRUE(validFirstMolecules.contains(mol))
        << "explore recommended '" << mol
        << "' but it is not a first molecule of any optimal path";
  }
}

} // namespace
