// tests/Session/ExploreTest.cpp
//
// Tests for Explore::Session: phase machine + recommendations + applyMotion
// + strict-revert buffer-state flow. Invalid phase is not a reachable state
// — programming-invariant failures assert, external teardown destroys the
// session — so there's no corresponding test here.

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "Boundary/MotionBoundary.h"
#include "Keyboard/Config.h"
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

  Explore::Session makeSession(
      Lines initial, CursorPos initialPos,
      Lines goal, CursorPos goalPos) {
    MotionBoundary boundary(initial,
        CursorPos(0, 0),
        CursorPos(static_cast<int>(initial.size()) - 1,
                  static_cast<int>(initial.back().size()) + 1),
        /*hasLinesAbove=*/false,
        /*hasLinesBelow=*/false);
    return Explore::Session(
        std::move(initial), initialPos,
        std::move(goal), goalPos,
        std::move(boundary),
        navContext,
        config);
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

TEST_F(ExploreSessionTest, RecommendationsAreDiverseAndSorted) {
  Lines initial{Line("foo bar baz qux zed")};
  Lines goal{Line("foo bar baz qux ZED")};
  auto session = makeSession(initial, {0, 0}, goal, {0, 18});

  ASSERT_EQ(session.step().kind, Explore::Phase::ApproachEdit);
  auto recs = session.recommendations(5);
  ASSERT_FALSE(recs.empty()) << "expected at least one motion recommendation";

  // Sorted by totalPathCost ascending.
  for (size_t i = 1; i < recs.size(); ++i) {
    EXPECT_LE(recs[i - 1].totalPathCost, recs[i].totalPathCost);
  }

  // Distinct recommendation texts — grouping/dedup works.
  set<string> texts;
  for (const auto& rec : recs) texts.insert(rec.text);
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

  auto outcome = session.applyMotion("<");  // incomplete special key
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

}  // namespace
