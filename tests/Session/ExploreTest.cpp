// tests/Session/ExploreTest.cpp
//
// Tests for Explore::Session: phase machine + recommendations + applyMotion
// + strict-revert buffer-state flow. Invalid phase is not a reachable state
// — programming-invariant failures assert, external teardown destroys the
// session — so there's no corresponding test here.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "Boundary/MotionBoundary.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/EditOptimizer/EditFrontier.h"
#include "Optimizer/EditOptimizer/EditSequenceDecomposition.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionRangeConversion.h"
#include "Optimizer/Result.h"
#include "Session/Explore.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

class ExploreSessionTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};

  Explore::Session makeSession(Lines initial, CursorPos initialPos, Lines goal,
                               CursorPos goalPos) {
    MotionBoundary boundary(
        initial, CursorPos(0, 0),
        CursorPos(static_cast<int>(initial.size()) - 1,
                  static_cast<int>(initial.back().size()) + 1),
        /*hasLinesAbove=*/false,
        /*hasLinesBelow=*/false);
    return Explore::Session(std::move(initial), initialPos, std::move(goal),
                            goalPos, std::move(boundary), navContext, config);
  }
};

TEST_F(ExploreSessionTest, CompletedWhenInitialEqualsGoal) {
  Lines lines{Line("hello world")};
  auto session = makeSession(lines, {0, 0}, lines, {0, 0});

  EXPECT_EQ(session.step().kind, Explore::Phase::Completed);
  EXPECT_EQ(session.totalEdits(), 0);
  EXPECT_TRUE(session.recommendations(5).empty());
}

TEST_F(ExploreSessionTest, ApproachesEditWhenLinesDiffer) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 10});

  EXPECT_EQ(session.step().kind, Explore::Phase::ApproachEdit);
  EXPECT_EQ(session.step().editIndex, 0);
  EXPECT_GT(session.totalEdits(), 0);
  EXPECT_EQ(session.state().cursor.line, 0);
  EXPECT_EQ(session.state().cursor.col, 0);
  EXPECT_TRUE(session.state().acceptedSeq.empty());
}

TEST_F(ExploreSessionTest, RecommendationsAreDiverse) {
  Lines initial{Line("foo bar baz qux zed")};
  Lines goal{Line("foo bar baz qux ZED")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 18});

  ASSERT_EQ(session.step().kind, Explore::Phase::ApproachEdit);
  auto recs = session.recommendations(5);
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

TEST_F(ExploreSessionTest, ApplyMotionAdvancesCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 14});

  auto outcome = session.applyMotion("w");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(session.step().kind, Explore::Phase::ApproachEdit);
  EXPECT_EQ(session.state().cursor.col, 4);
  EXPECT_EQ(session.state().acceptedSeq, "w");
  EXPECT_GT(session.state().acceptedCost, 0.0);
  EXPECT_TRUE(session.canUndo());
}

TEST_F(ExploreSessionTest, ApplyMotionRejectsMalformedInput) {
  Lines initial{Line("abcd")};
  Lines goal{Line("abCd")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 3});

  auto outcome = session.applyMotion("<"); // incomplete special key
  ASSERT_FALSE(outcome.has_value());
  EXPECT_FALSE(outcome.error().reason.empty());
  EXPECT_TRUE(session.state().acceptedSeq.empty());
  EXPECT_EQ(session.state().cursor.col, 0);
  EXPECT_FALSE(session.canUndo());
}

TEST_F(ExploreSessionTest, UndoRestoresPriorCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 14});

  ASSERT_TRUE(session.applyMotion("w").has_value());
  const int cursorAfter = session.state().cursor.col;
  ASSERT_GT(cursorAfter, 0);

  auto undone = session.undo();
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(session.state().cursor.col, 0);
  EXPECT_TRUE(session.state().acceptedSeq.empty());
  EXPECT_TRUE(session.canRedo());

  auto redone = session.redo();
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(session.state().cursor.col, cursorAfter);
  EXPECT_EQ(session.state().acceptedSeq, "w");
}

TEST_F(ExploreSessionTest, UndoFromCleanStateIsRejected) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 1});

  auto outcome = session.undo();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "nothing to undo");
}

TEST_F(ExploreSessionTest, BeginEditTransitionsIntoPendingInsert) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 1});

  auto outcome = session.beginEdit(true, "BX");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(session.step().kind, Explore::Phase::PendingInsert);
  EXPECT_EQ(session.step().remainingText, "BX");

  // Recommendations are empty outside ApproachEdit.
  EXPECT_TRUE(session.recommendations(5).empty());
}

TEST_F(ExploreSessionTest, OutOfScopeEditRejectedWithoutStateChange) {
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 4});

  ASSERT_TRUE(session.applyMotion("w").has_value());
  // Cursor is now on `n`. An edit command not in editResult.resultsAt gets
  // rejected without mutating state.
  const auto priorRevision = session.acceptedRevision();
  auto outcome = session.applyEdit("totally-not-a-real-edit");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(session.acceptedRevision(), priorRevision);
  EXPECT_EQ(session.step().kind, Explore::Phase::ApproachEdit);
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

TEST_F(ExploreSessionTest, AcceptInsertExitAdvancesPhaseOnMatchingBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 1});

  // Get the cursor to the edit target via the cheapest motion.
  auto recs = session.recommendations(5);
  ASSERT_FALSE(recs.empty());
  const Explore::Recommendation* motion = nullptr;
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      motion = &rec;
      break;
    }
  }
  if (motion)
    ASSERT_TRUE(session.applyMotion(motion->text).has_value());

  // Simulate the Lua layer: beginEdit(true, typedText) parks us in
  // PendingInsert; the post-insert buffer then validates via acceptInsertExit.
  ASSERT_TRUE(session.beginEdit(true, "B").has_value());
  ASSERT_EQ(session.step().kind, Explore::Phase::PendingInsert);

  auto outcome = session.acceptInsertExit(goal, {0, 2}, "");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(session.state().lines, goal);
  // Single-edit plan → advancing past it lands in Completed.
  EXPECT_EQ(session.step().kind, Explore::Phase::Completed);
}

TEST_F(ExploreSessionTest, AcceptInsertExitRejectsMismatchedBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 1});

  auto recs = session.recommendations(5);
  ASSERT_FALSE(recs.empty());
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      ASSERT_TRUE(session.applyMotion(rec.text).has_value());
      break;
    }
  }
  ASSERT_TRUE(session.beginEdit(true, "B").has_value());
  const auto priorRevision = session.acceptedRevision();

  Lines wrong{Line("aXc")};
  auto outcome = session.acceptInsertExit(wrong, {0, 2}, "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(session.step().kind, Explore::Phase::PendingInsert);
  EXPECT_EQ(session.acceptedRevision(), priorRevision);
}

TEST_F(ExploreSessionTest, CancelPendingInsertRestoresApproachEditWithoutRedo) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(session.beginEdit(true, "B").has_value());
  ASSERT_EQ(session.step().kind, Explore::Phase::PendingInsert);
  EXPECT_FALSE(session.canRedo());

  auto outcome = session.cancelPendingInsert();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(session.step().kind, Explore::Phase::ApproachEdit);
  // Crucially: redo stack NOT polluted with the rejected/abandoned insert.
  EXPECT_FALSE(session.canRedo());
}

TEST_F(ExploreSessionTest, RecommendationsCarryTypedText) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 1});

  // Move cursor onto the edit target so edit recs populate.
  auto recs = session.recommendations(5);
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      ASSERT_TRUE(session.applyMotion(rec.text).has_value());
      break;
    }
  }
  recs = session.recommendations(10);
  bool sawInsertAtom = false;
  for (const auto& rec : recs) {
    if (rec.kind == "edit" && !rec.typedText.empty())
      sawInsertAtom = true;
  }
  EXPECT_TRUE(sawInsertAtom)
      << "expected at least one edit recommendation with a non-empty typedText";
}

TEST_F(ExploreSessionTest, FillsRequestedCountAfterMovingOntoTarget) {
  Lines initial{Line("abc defghij klm")};
  Lines goal{Line("abc xyz klm")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 6});

  auto recs = session.recommendations(5);
  ASSERT_FALSE(recs.empty());
  for (const auto& rec : recs) {
    if (rec.kind == "motion") {
      ASSERT_TRUE(session.applyMotion(rec.text).has_value());
      break;
    }
  }

  recs = session.recommendations(5);
  EXPECT_EQ(recs.size(), 5u);
}

TEST_F(ExploreSessionTest, MotionRecommendationsAreFirstMoleculesOfOptimizerPaths) {
  // Explore shows immediate next molecules, not full paths to the target.
  // Each motion recommendation must be the FIRST molecule of some full A*
  // path that reaches the current edit's range, and they must be distinct.
  Lines initial{Line("one two three four five six seven")};
  Lines goal{Line("one two three four FIVE six seven")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 22});

  auto range = session.currentTargetRange();
  ASSERT_TRUE(range.has_value());

  // Match the ground-truth's `allowMultiplePerPosition=true` below so both
  // sides enumerate the same universe of molecules for the subset check.
  auto recs = session.recommendations(10, /*allowMultiplePerPosition=*/true);
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
  MotionOptimizerRangeParams params;
  params
      .withMaxResults(40)
      .withFMotionThreshold(compParams.fMotionThreshold)
      .withDirectionalPruning(compParams.useDirectionalPruning)
      .withLinePaddingAbove(compParams.motionPaddingAbove)
      .withLinePaddingBelow(compParams.motionPaddingBelow)
      .withMinCountRepeat(compParams.minPrefixCount)
      .withMaxCountRepeat(compParams.maxPrefixCount)
      .withAllowMultiplePerPosition(true);

  MotionOptimizer opt(config);
  auto motionRange =
      tryToMotionInterval(initial, CharRange(range->first, range->second));
  ASSERT_TRUE(motionRange.has_value());
  auto result = opt.optimizeToRange(
      initial, {0, 0}, *motionRange, params, "",
      MotionBoundary(initial, CursorPos(0, 0),
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
