// tests/MotionOptimizer/ManualTest.cpp
//
// Manual tests for MotionOptimizer with hardcoded setups.
// Tests horizontal/vertical motions, range optimization, and boundary constraints.
// For random/stress tests, see OutputCorrectnessTest.cpp.

#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Utils/TestUtils.h"

#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Boundary/MotionBoundary.h"
#include "Optimizer/MotionOptimizer.h"
#include "State/RunningEffort.h"
#include "Editor/Snapshot.h"
#include "Editor/Motion.h"
#include "Utils/Lines.h"

using namespace std;

class MotionOptimizer_ManualTest : public ::testing::Test {
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

    MotionOptimizer opt(config);

    // Tests use full test files, so don't exclude G/gg (default MotionBoundary)
    MotionBoundary boundary;
    // Pass Position and fresh RunningEffort (no prior typing context in tests)
    // Try to explore more (30 results), lower search depth for speed (2e4)
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        boundary, allowedMotions, OptimizerParams{.maxResults = 30, .maxNodesExplored = 20000}).results;
  }

  static vector<RangeResult>
  runOptimizerToRange(const Lines &lines, Position start,
                      Position rangeBegin, Position rangeEnd,
                      const string &userSeq,
                      int maxResults = 10,
                      const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
                      Config config = Config::uniform()) {
    MotionOptimizer opt(config);
    MotionBoundary boundary;
    // allowMultiplePerPosition=true for tests to see all paths
    // Pass Position and fresh RunningEffort (no prior typing context in tests)
    return opt.optimizeToRange(lines, start, RunningEffort(), rangeBegin, rangeEnd,
                               userSeq, navContext, true, boundary, allowedMotions,
                               OptimizerParams(maxResults, 2e4, 1.0, 2.0)).results;
  }
};

// Static member definitions
Lines MotionOptimizer_ManualTest::a1_long_line;
Lines MotionOptimizer_ManualTest::a2_block_lines;
Lines MotionOptimizer_ManualTest::a3_spaced_lines;
Lines MotionOptimizer_ManualTest::m1_main_basic;
NavContext MotionOptimizer_ManualTest::navContext;

TEST_F(MotionOptimizer_ManualTest, HorizontalMotions) {
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


// TODO: Re-enable when adding support for filtering the universe of explorable motions.
// This test was designed to verify optimizer behavior with a restricted motion set.
// Currently, all motions are explored automatically via MotionToSpec.
TEST_F(MotionOptimizer_ManualTest, DISABLED_VerticalMotions) {
}

// =============================================================================
// optimizeToRange tests
// =============================================================================

TEST_F(MotionOptimizer_ManualTest, RangeBasic_SameLine) {
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

TEST_F(MotionOptimizer_ManualTest, RangeBasic_MultiLine) {
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

TEST_F(MotionOptimizer_ManualTest, RangeFromMiddle) {
  // Start from middle of file, target range at end
  Lines lines = {"aaa", "bbb", "ccc", "ddd", "eee"};
  Position start(2, 1);
  Position rangeBegin(4, 0);
  Position rangeEnd(4, 2);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
}

TEST_F(MotionOptimizer_ManualTest, RangeWithWordMotions) {
  // Test that word motions can land in range
  Lines lines = {"one two three four five six"};
  Position start(0, 0);
  Position rangeBegin(0, 8);   // "three" starts at 8
  Position rangeEnd(0, 17);    // "four" ends at 17

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "www");

  EXPECT_FALSE(results.empty()) << "Should find paths using word motions";
}

// =============================================================================
// MotionBoundary tests
// =============================================================================

class MotionBoundaryTest : public ::testing::Test {
protected:
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  // Helper to run optimizer with specific boundary
  static vector<Result>
  runWithBoundary(const Lines& lines, Position start, Position end,
                  const string& userSeq, const MotionBoundary& boundary,
                  const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
                  Config config = Config::uniform()) {
    MotionOptimizer opt(config);
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        boundary, allowedMotions, OptimizerParams{.maxResults = 30, .maxNodesExplored = 20000}).results;
  }

  // Helper to check if results contain a sequence
  static bool hasSequence(const vector<Result>& results, const string& seq) {
    return std::any_of(results.begin(), results.end(),
        [&seq](const Result& r) { return r.getSequenceString() == seq; });
  }
};

NavContext MotionBoundaryTest::navContext(0, 0);

TEST_F(MotionBoundaryTest, DefaultBoundary_AllowsGG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  Position start(2, 0);
  Position end(0, 0);  // gg should reach this

  MotionBoundary boundary;  // default: no exclusions

  auto results = runWithBoundary(lines, start, end, "kk", boundary,
                                 getSlicedMotionToKeys({"j", "k", "gg"}));

  EXPECT_TRUE(hasSequence(results, "gg")) << "Default boundary should allow gg";
}

TEST_F(MotionBoundaryTest, ExcludeGG_RemovesGG) {
  // Full buffer has lines above the sub-region we're working with
  Lines fullBuffer = {"above0", "above1", "line0", "line1", "line2", "line3"};
  // Sub-buffer is lines 2-5 (line0 through line3)
  Lines subBuffer = {"line0", "line1", "line2", "line3"};
  Position start(2, 0);  // In sub-buffer coords
  Position end(0, 0);

  // Boundary computed from sub-region within full buffer
  // firstPos=(2,0) means hasLinesAbove=true, lastPos=(5,5) means hasLinesBelow=false
  MotionBoundary boundary(fullBuffer, Position(2, 0), Position(5, 5));

  auto results = runWithBoundary(subBuffer, start, end, "kk", boundary,
                                 getSlicedMotionToKeys({"j", "k", "gg"}));

  EXPECT_FALSE(hasSequence(results, "gg")) << "Boundary with hasLinesAbove should exclude gg";
  EXPECT_TRUE(hasSequence(results, "kk")) << "Should still find alternative path";
}

TEST_F(MotionBoundaryTest, DefaultBoundary_AllowsG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  Position start(1, 0);
  Position end(3, 0);  // G should reach last line

  MotionBoundary boundary;  // default: no exclusions

  auto results = runWithBoundary(lines, start, end, "jj", boundary,
                                 getSlicedMotionToKeys({"j", "k", "G"}));

  EXPECT_TRUE(hasSequence(results, "G")) << "Default boundary should allow G";
}

TEST_F(MotionBoundaryTest, ExcludeG_RemovesG) {
  // Full buffer has lines below the sub-region we're working with
  Lines fullBuffer = {"line0", "line1", "line2", "line3", "below0", "below1"};
  // Sub-buffer is lines 0-3
  Lines subBuffer = {"line0", "line1", "line2", "line3"};
  Position start(1, 0);
  Position end(3, 0);

  // Boundary computed from sub-region: firstPos=(0,0) hasLinesAbove=false, lastPos=(3,5) hasLinesBelow=true
  MotionBoundary boundary(fullBuffer, Position(0, 0), Position(3, 5));

  auto results = runWithBoundary(subBuffer, start, end, "jj", boundary,
                                 getSlicedMotionToKeys({"j", "k", "G"}));

  EXPECT_FALSE(hasSequence(results, "G")) << "Boundary with hasLinesBelow should exclude G";
  EXPECT_TRUE(hasSequence(results, "jj")) << "Should still find alternative path";
}

TEST_F(MotionBoundaryTest, LeftColOffset_FiltersPrefixPositions) {
  // NOTE: Position-based column filtering was removed because it was ineffective
  // for motions that clamp to buffer edges (paragraph, sentence jumps).
  // Single-line column filtering is a known limitation - the optimizer will
  // find paths to prefix/suffix positions on single lines.
  //
  // This test documents current behavior, not desired behavior.
  Lines lines = {"prefix_target"};
  Position start(0, 10);  // Start in "target" region
  Position end(0, 5);     // End in prefix region

  // leftColOffset = 7 (prefix "prefix_" length)
  // Boundary from position (0,7) to (0,12) gives leftColOffset=7
  MotionBoundary boundary(lines, Position(0, 7), Position(0, 12));

  auto results = runWithBoundary(lines, start, end, "hhhhh", boundary,
                                 getSlicedMotionToKeys({"h", "l"}));

  // With filtering removed, the optimizer WILL find a path to the prefix region
  EXPECT_FALSE(results.empty())
      << "Without filtering, optimizer finds path to prefix positions";

  // Should still find path to valid position
  Position end2(0, 8);  // Valid position after prefix
  auto results2 = runWithBoundary(lines, start, end2, "hh", boundary,
                                  getSlicedMotionToKeys({"h", "l"}));
  EXPECT_FALSE(results2.empty()) << "Should find path to valid positions";
}

TEST_F(MotionBoundaryTest, RightColOffset_FiltersSuffixPositions) {
  // NOTE: Position-based column filtering was removed because it was ineffective
  // for motions that clamp to buffer edges (paragraph, sentence jumps).
  // Single-line column filtering is a known limitation - the optimizer will
  // find paths to prefix/suffix positions on single lines.
  //
  // This test documents current behavior, not desired behavior.
  Lines lines = {"target_suffix"};  // length=13, suffix "_suffix" starts at col 7
  Position start(0, 3);   // Start in "target" region
  Position end(0, 10);    // End in suffix region

  // rightColOffset = 6 (suffix "_suffix" without the 's' at col 7)
  // Boundary from position (0,0) to (0,6) gives rightColOffset = 13-1-6 = 6
  MotionBoundary boundary(lines, Position(0, 0), Position(0, 6));

  auto results = runWithBoundary(lines, start, end, "lllllll", boundary,
                                 getSlicedMotionToKeys({"h", "l"}));

  // With filtering removed, the optimizer WILL find a path to the suffix region
  EXPECT_FALSE(results.empty())
      << "Without filtering, optimizer finds path to suffix positions";

  // Should still find path to valid position
  Position end2(0, 6);  // Valid position before suffix (col 7 is where suffix starts)
  auto results2 = runWithBoundary(lines, start, end2, "lll", boundary,
                                  getSlicedMotionToKeys({"h", "l"}));
  EXPECT_FALSE(results2.empty()) << "Should find path to valid positions";
}

TEST_F(MotionBoundaryTest, IsPositionInBounds_WithColConstraints) {
  // Test isPositionInBounds with column constraints
  // leftColOffset=3 (prefix length on first line)
  // rightColOffset=5 (suffix length on last line)
  // lastLine=2, lastLineLength=15 (so suffix starts at col 15-5=10)

  // Build a 3-line buffer where:
  // - line 0 has prefix of 3 chars, so firstPos=(0,3)
  // - line 2 has length 15, suffix of 5 chars, so lastPos=(2,9) (last valid col is 14-5=9)
  Lines lines = {"pppxxxxxx", "middle_content", "xxxxxxxxxxxxxxx"};  // line 2 has 15 chars
  // Boundary from (0,3) to (2,9): leftColOffset=3, rightColOffset=15-1-9=5
  MotionBoundary boundary(lines, Position(0, 3), Position(2, 9));

  int lastLine = 2;
  int lastLineLength = 15;

  // First line (line 0): positions < leftColOffset are prefix
  EXPECT_FALSE(boundary.isPositionInBounds(Position(0, 0), lastLine, lastLineLength)) << "col 0 < leftColOffset";
  EXPECT_FALSE(boundary.isPositionInBounds(Position(0, 2), lastLine, lastLineLength)) << "col 2 < leftColOffset";
  EXPECT_TRUE(boundary.isPositionInBounds(Position(0, 3), lastLine, lastLineLength)) << "col 3 == leftColOffset (first valid)";
  EXPECT_TRUE(boundary.isPositionInBounds(Position(0, 5), lastLine, lastLineLength)) << "col 5 > leftColOffset";

  // Middle line: no column constraints
  EXPECT_TRUE(boundary.isPositionInBounds(Position(1, 0), lastLine, lastLineLength));
  EXPECT_TRUE(boundary.isPositionInBounds(Position(1, 10), lastLine, lastLineLength));

  // Last line (line 2): positions >= lastLineLength - rightColOffset = 10 are suffix
  EXPECT_TRUE(boundary.isPositionInBounds(Position(2, 5), lastLine, lastLineLength)) << "col 5 < 10";
  EXPECT_TRUE(boundary.isPositionInBounds(Position(2, 9), lastLine, lastLineLength)) << "col 9 < 10 (last valid)";
  EXPECT_FALSE(boundary.isPositionInBounds(Position(2, 10), lastLine, lastLineLength)) << "col 10 == 10 (first in suffix)";
  EXPECT_FALSE(boundary.isPositionInBounds(Position(2, 14), lastLine, lastLineLength)) << "col 14 > 10";
}

// =============================================================================
// Note: Stress tests (random buffers) are in OutputCorrectnessTest.cpp
// =============================================================================
