#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Utils/TestUtils.h"

#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/ImpliedExclusions.h"
#include "Optimizer/MovementOptimizer.h"
#include "State/RunningEffort.h"
#include "Editor/Snapshot.h"
#include "Editor/Motion.h"
#include "Utils/Lines.h"

using namespace std;

class MovementOptimizerTest : public ::testing::Test {
protected:
  static Lines a1_long_line;
  static Lines a2_block_lines;
  static Lines a3_spaced_lines;
  static Lines m1_main_basic;
  static NavContext navContext;

  static void SetUpTestSuite() {
      a1_long_line = TestFiles::load("a1_long_line.txt");
      a2_block_lines = TestFiles::load("a2_block_lines.txt");
      a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
      m1_main_basic = TestFiles::load("m1_main_basic.txt");
      navContext = NavContext();
  }

  static vector<Result>
  runOptimizer(const Lines &lines, Position start,
               Position end, const string &userSeq,
               const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
               vector<KeyAdjustment> adjustments = {},
               Config config = Config::uniform()
               ) {
    for(KeyAdjustment ka : adjustments) {
      config.keyInfo[static_cast<size_t>(ka.k)].base_cost = ka.cost;
    }

    MovementOptimizer opt(config);

    // Tests use full test files, so don't exclude G/gg
    ImpliedExclusions impliedExclusions(false, false);
    // Pass Position and fresh RunningEffort (no prior typing context in tests)
    // Try to explore more (30 results), lower search depth for speed (2e4)
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        impliedExclusions, allowedMotions, OptimizerParams(30, 2e4, 1.0, 2.0));
  }

  static vector<RangeResult>
  runOptimizerToRange(const Lines &lines, Position start,
                      Position rangeBegin, Position rangeEnd,
                      const string &userSeq,
                      int maxResults = 10,
                      const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
                      Config config = Config::uniform()) {
    MovementOptimizer opt(config);
    ImpliedExclusions impliedExclusions(false, false);
    // allowMultiplePerPosition=true for tests to see all paths
    // Pass Position and fresh RunningEffort (no prior typing context in tests)
    return opt.optimizeToRange(lines, start, RunningEffort(), rangeBegin, rangeEnd,
                               userSeq, navContext, true, impliedExclusions, allowedMotions,
                               OptimizerParams(maxResults, 2e4, 1.0, 2.0));
  }
};

// Static member definitions
Lines MovementOptimizerTest::a1_long_line;
Lines MovementOptimizerTest::a2_block_lines;
Lines MovementOptimizerTest::a3_spaced_lines;
Lines MovementOptimizerTest::m1_main_basic;
NavContext MovementOptimizerTest::navContext;

TEST_F(MovementOptimizerTest, HorizontalMotions) {
  const string user_seq = "we";
  Position start(0, 0);
  Position end = simulateMotions(start, user_seq, a1_long_line);

  vector<Result> results = runOptimizer(
  a1_long_line,
    start, end, user_seq
  );

  // Note: "2e" and "ee" are functionally equivalent; optimizer may prefer count-prefixed
  // f motions may not be explored within result limit depending on search order
  EXPECT_TRUE(contains_all(results, {user_seq, "wE", "2e", "2E"}))
      << "Missing expected sequences";
}


TEST_F(MovementOptimizerTest, VerticalMotions) {
  const string user_seq = "jjjjj";
  Position start(2, 0);
  Position end = simulateMotions(start, user_seq, a3_spaced_lines);

  vector<Result> results = runOptimizer(
    a3_spaced_lines,
    start, end, user_seq,
    getSlicedMotionToKeys({"j", "k", "G", "{", "}", "(", ")"})
  );
  // printResults(results);
  EXPECT_TRUE(contains_all(results, {"Gk", "G{", "}}", "})}", "}jjj"}))
      << "Missing expected sequences";
}

// =============================================================================
// optimizeToRange tests
// =============================================================================

TEST_F(MovementOptimizerTest, RangeBasic_SameLine) {
  // Target range is columns 5-10 on line 0
  Lines lines = {"hello world this is a test line"};
  Position start(0, 0);
  Position rangeBegin(0, 5);
  Position rangeEnd(0, 10);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "lllll");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
  for (const auto& r : results) {
    EXPECT_GE(r.endPos.col, 5) << "End position should be in range";
    EXPECT_LE(r.endPos.col, 10) << "End position should be in range";
  }
}

TEST_F(MovementOptimizerTest, RangeBasic_MultiLine) {
  // Target range spans multiple lines
  Lines lines = {"line one", "line two", "line three", "line four"};
  Position start(0, 0);
  Position rangeBegin(1, 0);
  Position rangeEnd(2, 5);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
  for (const auto& r : results) {
    Position p = r.endPos;
    bool inRange = (p >= rangeBegin && p <= rangeEnd);
    EXPECT_TRUE(inRange) << "End position (" << p.line << ", " << p.col << ") should be in range";
  }
}

TEST_F(MovementOptimizerTest, RangeFromMiddle) {
  // Start from middle of file, target range at end
  Lines lines = {"aaa", "bbb", "ccc", "ddd", "eee"};
  Position start(2, 1);
  Position rangeBegin(4, 0);
  Position rangeEnd(4, 2);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
}

TEST_F(MovementOptimizerTest, RangeWithWordMotions) {
  // Test that word motions can land in range
  Lines lines = {"one two three four five six"};
  Position start(0, 0);
  Position rangeBegin(0, 8);   // "three" starts at 8
  Position rangeEnd(0, 17);    // "four" ends at 17

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "www");

  EXPECT_FALSE(results.empty()) << "Should find paths using word motions";
}
