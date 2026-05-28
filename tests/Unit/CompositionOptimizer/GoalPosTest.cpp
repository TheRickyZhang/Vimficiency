// tests/Unit/CompositionOptimizer/GoalPosTest.cpp
//
// Stage-1 tests for CompositionOptimizer's explicit goalPos handling: the
// optimizer now treats `goalPos` as a real terminal target, with E+1 nav
// segments interleaved among E edits. Pure-motion sessions (E == 0) flow
// through the optimizer too, producing a one-nav-segment plan to goalPos.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="CompositionOptimizerGoalPos*"

#include <gtest/gtest.h>
#include <memory>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

using namespace std;

class CompositionOptimizerGoalPosTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  void verifyGoalReached(
      const Result& result, const Lines& initial, CursorPos initialPos,
      const Lines& goal, CursorPos goalPos, const string& context) {
    EXPECT_TRUE(OracleReplay::matches(
        *oracle, initial, initialPos, result.getSequence().str(),
        goal, goalPos, Mode::Normal, context));
  }
};

unique_ptr<NeovimOracle> CompositionOptimizerGoalPosTest::oracle;

// =============================================================================
// Plan API: navTarget(int) and totalNavigations()
// =============================================================================

TEST_F(CompositionOptimizerGoalPosTest, PlanApi_NavTargetExposesGoalPosAtTotalEdits) {
  Lines initial = {"hello world"};
  Lines goal = {"hello there"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 10);

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  const auto& plan = res.getPlan();

  EXPECT_EQ(plan.totalNavigations(), plan.totalEdits() + 1);
  EXPECT_EQ(res.getGoalPos(), goalPos)
      << "getGoalPos() must reflect the user's goalPos, not the implicit last-edit cursor";

  CharRange finalNav = plan.navTarget(plan.totalEdits());
  EXPECT_EQ(finalNav.begin, goalPos);
  EXPECT_EQ(finalNav.end, goalPos);
}

TEST_F(CompositionOptimizerGoalPosTest, PlanApi_PureMotionHasOneNavTarget) {
  Lines initial = {"hello world"};
  Lines goal = {"hello world"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 10);

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  const auto& plan = res.getPlan();

  EXPECT_EQ(plan.totalEdits(), 0);
  EXPECT_EQ(plan.totalNavigations(), 1);
  EXPECT_EQ(plan.navTarget(0).begin, goalPos);
}

// =============================================================================
// Pure-motion (E == 0): optimizer handles motion directly
// =============================================================================

TEST_F(CompositionOptimizerGoalPosTest, PureMotion_FindsMotionSequence) {
  Lines initial = {"hello world"};
  Lines goal = {"hello world"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 10);
  NavBoundary boundary(initial, initialPos, initial.endPos());

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos,
                                       CompositionOptimizerParams{}, "", boundary);
  const auto& results = res.getResults();
  ASSERT_FALSE(results.empty()) << "Optimizer should produce a motion sequence for pure motion";
  EXPECT_EQ(res.totalEdits(), 0);

  for (size_t i = 0; i < results.size(); i++) {
    verifyGoalReached(results[i], initial, initialPos, goal, goalPos,
                      "PureMotion result " + to_string(i));
  }
}

TEST_F(CompositionOptimizerGoalPosTest, PureMotion_TrivialCompletionEmitsVacuousResult) {
  // initialPos == goalPos with identical buffers: nothing to do. The optimizer
  // emits a single empty-sequence result expressing the trivially-satisfied
  // path. Consumers can detect this via getSequence().empty().
  Lines initial = {"hello world"};
  Lines goal = {"hello world"};
  CursorPos pos(0, 4);

  CompositionResult res = opt.optimize(initial, pos, goal, pos);

  ASSERT_EQ(res.getResults().size(), 1u);
  EXPECT_TRUE(res.getResults()[0].getSequence().empty())
      << "Trivial completion should be expressed as an empty-sequence result";
  EXPECT_EQ(res.totalEdits(), 0);
  EXPECT_EQ(res.getGoalPos(), pos);
}

// =============================================================================
// Final-nav cost is included in plan ranking
// =============================================================================

TEST_F(CompositionOptimizerGoalPosTest, FinalNavCost_CursorActuallyLandsAtGoalPos) {
  // Multi-edit session where the user's goalPos differs from the natural
  // post-last-edit cursor — verify replay through Neovim ends at goalPos.
  Lines initial = {"foo bar baz qux"};
  Lines goal = {"foo BAR baz QUX"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);  // Land back at start, not where the last edit leaves us
  NavBoundary boundary(initial, initialPos, initial.endPos());

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos,
                                       CompositionOptimizerParams{}, "", boundary);
  const auto& results = res.getResults();
  ASSERT_FALSE(results.empty()) << "Multi-edit + final-nav should produce results";

  // Every result must end with the cursor at goalPos, not at some convenient
  // post-edit landing. This is the property the old implicit-goalPos optimizer
  // could not guarantee.
  for (size_t i = 0; i < results.size(); i++) {
    verifyGoalReached(results[i], initial, initialPos, goal, goalPos,
                      "FinalNavCost result " + to_string(i));
  }
}

TEST_F(CompositionOptimizerGoalPosTest, SearchKeySeparatesStickyColumnBeforeEdit) {
  Lines initial = {
      "fcd e.. ,",
      "f.c cda. ",
      "a   ",
  };
  Lines goal = {
      ".f.bc",
      "fcd e.. ,",
      "f.cdaeeaeda. ",
  };
  CursorPos initialPos(0, 5);
  CursorPos goalPos(1, 3);

  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos,
      CompositionOptimizerParams{}.withMaxResults(5));
  const auto& results = res.getResults();
  ASSERT_FALSE(results.empty())
      << "Optimizer should find a composition plan for the fuzz regression";

  for (size_t i = 0; i < results.size(); i++) {
    verifyGoalReached(results[i], initial, initialPos, goal, goalPos,
                      "StickyColumnBeforeEdit result " + to_string(i));
  }
}

TEST_F(CompositionOptimizerGoalPosTest, WholeBufferRewriteClearsVisualChangeIndent) {
  Lines initial = {" db"};
  Lines goal = {" ", "db"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(1, 1);

  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos,
      CompositionOptimizerParams{}.withMaxResults(5));
  const auto& results = res.getResults();
  ASSERT_FALSE(results.empty())
      << "Optimizer should handle a multiline rewrite with leading space";

  for (size_t i = 0; i < results.size(); i++) {
    verifyGoalReached(results[i], initial, initialPos, goal, goalPos,
                      "WholeBufferRewriteClearsVisualChangeIndent result " +
                          to_string(i));
  }
}
