// tests/MotionOptimizer/HumanApprovalTest.cpp
//
// Human-verified examples for MotionOptimizer output quality.
// Since no ground truth optimizer exists, we manually verify that outputs
// are sensible and contain expected efficient sequences.
//
// Workflow:
// 1. Run test with DISABLED_ prefix first to see output
// 2. Verify output is sensible
// 3. Add assertions for expected sequences
// 4. Remove DISABLED_ prefix

#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/MotionOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"
#include "Utils/TestUtils.h"  // hasSequence, hasSequenceStartingWith, printResultsDebug

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class MotionOptimizerHumanApprovalTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext MotionOptimizerHumanApprovalTests::navContext;

// =============================================================================
// MotionOptimizer Examples
// =============================================================================

TEST_F(MotionOptimizerHumanApprovalTests, DISABLED_Motion_SimpleHorizontal) {
  // Moving right 5 characters: "lllll" vs "5l" vs word motions
  Lines lines = {"hello world test"};
  Position start(0, 0);
  Position end(0, 5);  // At space before "world"

  MotionOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "lllll", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
  );

  printResultsDebug(results, "Simple horizontal movement 0→5");

  EXPECT_FALSE(results.empty()) << "Should find at least one path";

  // TODO: Add assertions after reviewing output
  // EXPECT_TRUE(hasSequence(results, "5l") || hasSequence(results, "lllll"));
}

TEST_F(MotionOptimizerHumanApprovalTests, Motion_VerticalJump) {
  // Moving down 3 lines
  Lines lines = {"line one", "line two", "line three", "line four"};
  Position start(0, 0);
  Position end(3, 0);

  MotionOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "jjj", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
  );

  // printResults(results, "Vertical jump 3 lines");

  EXPECT_FALSE(results.empty());

  // G goes to last line, should be found
  EXPECT_TRUE(hasSequence(results, "G"))
      << "G should be found for jumping to last line";

  // 3j should also be viable
  EXPECT_TRUE(hasSequence(results, "3j") || hasSequence(results, "jjj"))
      << "Should find count-based or repeated j";
}

TEST_F(MotionOptimizerHumanApprovalTests, Motion_WordMotions) {
  // Navigate using word motions
  Lines lines = {"one two three four five"};
  Position start(0, 0);
  Position end(0, 14);  // Start of "four"

  MotionOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "www", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
  );

  // printResults(results, "Word motions to 'four'");

  EXPECT_FALSE(results.empty());

  // Should find word-based navigation
  EXPECT_TRUE(hasSequence(results, "3w") || hasSequence(results, "www"))
      << "Should find word-based navigation";
}

TEST_F(MotionOptimizerHumanApprovalTests, Motion_MixedMotions) {
  // Complex movement requiring mixed motions
  Lines lines = {"first line here", "second line", "third line end"};
  Position start(0, 0);
  Position end(2, 11);  // At "end"

  MotionOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "jjllllllllll", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(15, 1e4, 1.0, 2.0)
  );

  // printResults(results, "Mixed motions to line 2, col 11");

  EXPECT_FALSE(results.empty());

  // Should find something more efficient than 12 l's
  bool foundEfficient = false;
  for (const auto& r : results) {
    if (r.getSequenceString().size() < 12) {
      foundEfficient = true;
      break;
    }
  }
  EXPECT_TRUE(foundEfficient) << "Should find sequence shorter than naive approach";
}
