// tests/CompositionOptimizer/ManualTest.cpp
//
// Manual tests for CompositionOptimizer with hardcoded setups.
// Verifies optimizer returns results and sequences achieve goal state.
// For random/stress tests, see OutputCorrectnessTest.cpp (when added).
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="CompositionOptimizer_ManualTest.*"

#include <gtest/gtest.h>
#include <memory>

#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class CompositionOptimizer_ManualTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  // Verify at least one result exists and all results achieve goal state
  void expectHasValidResults(
      const vector<Result>& results,
      const Lines& initial,
      Position startPos,
      const Lines& goal,
      const string& testContext = "") {

    ASSERT_FALSE(results.empty())
        << "No results returned" << (testContext.empty() ? "" : " (" + testContext + ")");

    for (size_t i = 0; i < results.size(); i++) {
      EXPECT_TRUE(results[i].isValid())
          << "Result " << i << " is invalid"
          << (testContext.empty() ? "" : " (" + testContext + ")");

      string seq = results[i].getSequenceString();
      SimulationResult nvim = oracle->simulate(initial, startPos.line, startPos.col, seq);

      EXPECT_EQ(nvim.lines, goal)
          << "Lines mismatch" << (testContext.empty() ? "" : " (" + testContext + ")")
          << " for result " << i << " seq='" << seq << "' from " << startPos << "\n"
          << "  Initial: " << initial << "\n"
          << "  Goal:    " << goal << "\n"
          << "  Got:     " << nvim.lines;
    }
  }
};

unique_ptr<NeovimOracle> CompositionOptimizer_ManualTest::oracle;

// =============================================================================
// Single Edit Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_SimpleSubstitution) {
  // Simple word change on same line
  Lines initial = {"hello world"};
  Lines goal = {"hello there"};
  Position initialPos(0, 0);
  Position goalPos(0, 10);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, initialPos, goal, "simple substitution");
}

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_AtCursor) {
  // Cursor already at edit position
  Lines initial = {"aaa"};
  Lines goal = {"bbb"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "at cursor");
}

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_CursorAfterEdit) {
  // Cursor is after the edit region - needs backward motion
  Lines initial = {"aaa bbb"};
  Lines goal = {"xxx bbb"};
  Position startPos(0, 6);  // Cursor at end

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "cursor after edit");
}

// =============================================================================
// Multiple Edit Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, TwoEdits_SameLine) {
  // Two changes on the same line
  Lines initial = {"aaa bbb ccc"};
  Lines goal = {"xxx bbb yyy"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "two edits same line");
}

TEST_F(CompositionOptimizer_ManualTest, DISABLED_TwoEdits_DifferentLines) {
  // TODO: Fix - replacement strategy produces incorrect sequence
  // Changes on different lines
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"xxx", "bbb", "yyy"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "two edits different lines");
}

// =============================================================================
// Pure Insertion / Deletion Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, PureInsertion) {
  Lines initial = {"hello"};
  Lines goal = {"hello world"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "pure insertion");
}

TEST_F(CompositionOptimizer_ManualTest, PureDeletion) {
  // Delete text, leaving nothing
  Lines initial = {"hello world"};
  Lines goal = {"hello"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "pure deletion");
}

// =============================================================================
// Line-Level Operations
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, DeleteEntireLine) {
  // Delete middle line
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"aaa", "ccc"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "delete entire line");
}

TEST_F(CompositionOptimizer_ManualTest, InsertNewLine) {
  // Insert a new line
  Lines initial = {"aaa", "ccc"};
  Lines goal = {"aaa", "bbb", "ccc"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "insert new line");
}

// =============================================================================
// No Change Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, NoChange_IdenticalBuffers) {
  // When initial == goal, should return empty result or no-op
  Lines initial = {"hello world"};
  Lines goal = {"hello world"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  // Empty results is valid (no changes needed)
  // If there are results, verify they don't break the buffer
  if (!results.empty()) {
    expectHasValidResults(results, initial, startPos, goal, "no change");
  }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_EmptyToContent) {
  // Empty line to content
  Lines initial = {""};
  Lines goal = {"hello"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "empty to content");
}

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_ContentToEmpty) {
  // Content to empty line
  Lines initial = {"hello"};
  Lines goal = {""};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "content to empty");
}

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_SingleCharChange) {
  // Single character substitution
  Lines initial = {"abc"};
  Lines goal = {"xbc"};
  Position startPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, startPos, goal, Position(0, 0), "", NavContext(),
      MotionBoundary(), EXPLORABLE_MOTIONS, params);

  expectHasValidResults(results, initial, startPos, goal, "single char change");
}

// =============================================================================
// Note: More comprehensive stress tests should be added in OutputCorrectnessTest.cpp
// =============================================================================
