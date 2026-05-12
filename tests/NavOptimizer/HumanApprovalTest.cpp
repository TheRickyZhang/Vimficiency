// tests/NavOptimizer/HumanApprovalTest.cpp
//
// Human-verified examples for NavOptimizer output quality.
// Since no ground truth optimizer exists, we manually verify that outputs
// are sensible and contain expected efficient sequences.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="NavOptimizerHumanApprovalTests.*"

#include <gtest/gtest.h>

#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "Utils/TestUtils.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class NavOptimizerHumanApprovalTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext NavOptimizerHumanApprovalTests::navContext;

// =============================================================================
// NavOptimizer Examples
// =============================================================================

TEST_F(NavOptimizerHumanApprovalTests, Motion_SimpleHorizontal) {
  Lines lines = {"hello world test"};
  CursorPos start(0, 0);
  CursorPos end(0, 5);  // At space before "world"

  NavOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, end,
    NavOptimizerParams{}.withMaxResultsPerEndPos(2),
    "lllll"
  ).getResults();
  // printResultsDebug(results, "Simple horizontal movement 0 to 5");

  EXPECT_FALSE(results.empty()) << "Should find at least one path";
  EXPECT_TRUE(hasSequence(results, "5l") || hasSequence(results, "lllll"))
      << "Should find the count-based horizontal movement or the original replay";
}

TEST_F(NavOptimizerHumanApprovalTests, Motion_VerticalJump) {
  // Moving down 3 lines
  Lines lines = {"line one", "line two", "line three", "line four"};
  CursorPos start(0, 0);
  CursorPos end(3, 0);

  NavOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, end,
    NavOptimizerParams{}.withMaxResultsPerEndPos(2),
    "jjj"
  ).getResults();

  // printResultsDebug(results, "Vertical jump 3 lines");

  EXPECT_FALSE(results.empty());

  // G goes to last line, should be found
  EXPECT_TRUE(hasSequence(results, "G"))
      << "G should be found for jumping to last line";

  // 3j should also be viable
  EXPECT_TRUE(hasSequence(results, "3j") || hasSequence(results, "jjj"))
      << "Should find count-based or repeated j";
}

TEST_F(NavOptimizerHumanApprovalTests, Motion_WordMotions) {
  // Navigate using word motions
  Lines lines = {"one two three four five"};
  CursorPos start(0, 0);
  CursorPos end(0, 14);  // Start of "four"

  NavOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, end,
    NavOptimizerParams{}.withMaxResultsPerEndPos(2),
    "www"
  ).getResults();

  // printResultsDebug(results, "Word motions to 'four'");

  EXPECT_FALSE(results.empty());

  // Should find some valid path to "four"
  EXPECT_FALSE(results[0].getSequence().empty())
      << "Should find word-based navigation";
}

TEST_F(NavOptimizerHumanApprovalTests, Motion_MixedMotions) {
  // Complex movement requiring mixed motions
  Lines lines = {"first line here", "second line", "third line end"};
  CursorPos start(0, 0);
  CursorPos end(2, 11);  // At "end"

  NavOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, end,
    NavOptimizerParams{}.withMaxResults(15).withMaxResultsPerEndPos(2),
    "jjllllllllll"
  ).getResults();

  // printResultsDebug(results, "Mixed motions to line 2, col 11");

  EXPECT_FALSE(results.empty());

  // Should find something more efficient than 12 l's
  bool foundEfficient = false;
  for (const auto& r : results) {
    if (r.getSequence().size() < 12) {
      foundEfficient = true;
      break;
    }
  }
  EXPECT_TRUE(foundEfficient) << "Should find sequence shorter than naive approach";
}
