// tests/DebugSequenceTests.cpp
//
// Debug tests for specific sequence issues that pop up during development.
// These tests help isolate and verify bugs without cluttering the main test files.

#include <gtest/gtest.h>

#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/ImpliedExclusions.h"
#include "Optimizer/Optimizer.h"
#include "State/RunningEffort.h"
#include "State/State.h"
#include "TestUtils.h"

using namespace std;

class DebugSequenceTest : public ::testing::Test {
protected:
  static vector<string> a2_block_lines;
  static NavContext navContext;

  static void SetUpTestSuite() {
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    navContext = NavContext(0, 39, 39, 19);
  }

  static State makeState(Position p) { return State(p, RunningEffort(), 0, 0); }

  static Position simulateMotionsDefault(Position start, const string &motion,
                                          const vector<string> &lines) {
    return simulateMotions(start, Mode::Normal, navContext, motion, lines).pos;
  }

  static void expectPos(Position actual, int line, int col,
                        const string &msg = "") {
    EXPECT_EQ(actual.line, line) << msg << " (line)";
    EXPECT_EQ(actual.col, col) << msg << " (col)";
  }

  static vector<Result> runOptimizer(const vector<string> &lines,
                                     Position start, Position end,
                                     const string &userSeq,
                                     Config config = Config::uniform()) {
    Optimizer opt(config, 30, 2e4, 1.0, 2.0);
    ImpliedExclusions impliedExclusions(false, false);
    return opt.optimizeMovement(lines, makeState(start), end, userSeq,
                                navContext, impliedExclusions);
  }

  // Check if a result contains a specific sequence
  static bool containsSequence(const vector<Result> &results,
                               const string &seq) {
    return std::any_of(results.begin(), results.end(),
                       [&seq](const Result &r) { return r.sequence == seq; });
  }
};

vector<string> DebugSequenceTest::a2_block_lines;
NavContext DebugSequenceTest::navContext(0, 0, 0, 0);

// =============================================================================
// Regression tests for motion behavior
//
// a2_block_lines has 4 identical lines of "aaaaaaaaaaaaaa" (14 chars each)
//
// These tests verify:
//   - e motion stops at line end (not crossing lines)
//   - Word motions correctly update targetCol for j/k column preservation
//   - Optimizer returns only sequences that actually reach target position
// =============================================================================

TEST_F(DebugSequenceTest, JjllEndsAtCorrectPosition) {
  // Verify jjll ends at (2, 2)
  Position result = simulateMotionsDefault({0, 0}, "jjll", a2_block_lines);
  expectPos(result, 2, 2, "jjll from (0,0)");
}

// Regression test: e motion should stop at line end (not cross lines)
TEST_F(DebugSequenceTest, EMotionStopsAtLineEnd) {
  // e from (0,0) should stop at (0, 13) - end of word on line 0
  Position result = simulateMotionsDefault({0, 0}, "e", a2_block_lines);
  expectPos(result, 0, 13, "e from (0,0) should stop at end of word on same line");
}

TEST_F(DebugSequenceTest, VerifyKAtLine0) {
  // Sanity check: k from line 0 stays at line 0
  Position result = simulateMotionsDefault({0, 5}, "k", a2_block_lines);
  expectPos(result, 0, 5, "k from line 0 stays at line 0");
}

// Once e is fixed, ekll should NOT reach (2, 2)
// e→(0,13), k→stays(0,13), ll→stays(0,13) = (0, 13)
TEST_F(DebugSequenceTest, EkllShouldNotReachJjllTarget) {
  Position result = simulateMotionsDefault({0, 0}, "ekll", a2_block_lines);

  // After fixing the e motion bug, this should be (0, 13)
  // Currently it's (2, 2) because e incorrectly goes to (3, 13)
  expectPos(result, 0, 13, "ekll should end at (0,13) not (2,2)");
}

// Test that w motion correctly handles line boundaries (reference for e fix)
TEST_F(DebugSequenceTest, WMotionStopsAtLineEnd) {
  // w should go to start of next word, treating newline as word boundary
  Position result = simulateMotionsDefault({0, 0}, "w", a2_block_lines);
  // On a2_block_lines with no spaces, w goes to next line
  expectPos(result, 1, 0, "w from (0,0) goes to next line start");
}

TEST_F(DebugSequenceTest, AllResultsReachTargetPosition) {
  Position start(0, 0);
  Position end(2, 2);

  vector<Result> results =
      runOptimizer(a2_block_lines, start, end, "jjll");

  // Print results for debugging
  cout << "Optimizer results for (0,0) -> (2,2):\n";
  for (const auto &r : results) {
    Position actual = simulateMotionsDefault(start, r.sequence, a2_block_lines);
    cout << "  " << r.sequence << " -> (" << actual.line << "," << actual.col << ")\n";
  }

  for (const auto &r : results) {
    Position actual = simulateMotionsDefault(start, r.sequence, a2_block_lines);
    EXPECT_EQ(actual.line, end.line)
        << "Sequence '" << r.sequence << "' ends at wrong line: "
        << actual.line << " vs expected " << end.line;
    EXPECT_EQ(actual.col, end.col)
        << "Sequence '" << r.sequence << "' ends at wrong col: "
        << actual.col << " vs expected " << end.col;
  }
}
