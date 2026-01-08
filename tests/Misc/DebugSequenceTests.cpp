// tests/DebugSequenceTests.cpp
//
// Debug tests for specific sequence issues that pop up during development.
// These tests help isolate and verify bugs without cluttering the main test files.

#include <gtest/gtest.h>

#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/ImpliedExclusions.h"
#include "Optimizer/MovementOptimizer.h"
#include "State/RunningEffort.h"
#include "Utils/TestUtils.h"

using namespace std;

class DebugSequenceTest : public ::testing::Test {
protected:
  static vector<string> a2_block_lines;
  static NavContext navContext;

  static void SetUpTestSuite() {
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    navContext = NavContext(39, 19);
  }

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
    MovementOptimizer opt(config);
    ImpliedExclusions impliedExclusions(false, false);
    return opt.optimize(lines, start, RunningEffort(), end, userSeq,
                        navContext, impliedExclusions, EXPLORABLE_MOTIONS, OptimizerParams(30, 2e4, 1.0, 2.0));
  }

  // Check if a result contains a specific sequence
  static bool containsSequence(const vector<Result> &results,
                               const string &seq) {
    return std::any_of(results.begin(), results.end(),
                       [&seq](const Result &r) { return r.getSequenceString() == seq; });
  }
};

vector<string> DebugSequenceTest::a2_block_lines;
NavContext DebugSequenceTest::navContext(0, 0);

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
    Position actual = simulateMotionsDefault(start, r.getSequenceString(), a2_block_lines);
    cout << "  " << r << " -> (" << actual.line << "," << actual.col << ")\n";
  }

  for (const auto &r : results) {
    Position actual = simulateMotionsDefault(start, r.getSequenceString(), a2_block_lines);
    EXPECT_EQ(actual.line, end.line)
        << "Sequence '" << makePrintable(r.getSequenceString()) << "' ends at wrong line: "
        << actual.line << " vs expected " << end.line;
    EXPECT_EQ(actual.col, end.col)
        << "Sequence '" << makePrintable(r.getSequenceString()) << "' ends at wrong col: "
        << actual.col << " vs expected " << end.col;
  }
}

// =============================================================================
// Bug: }ll returned as solution for jjll starting from (1,0)
//
// a2_block_lines: lines 0-3 = "aaaaaaaaaaaaaa", line 4 = empty
//
// From (1, 0):
//   - jjll: j→(2,0), j→(3,0), l→(3,1), l→(3,2) = ends at (3, 2)
//   - }ll: }→(4,0) [empty line], ll can't move = ends at (4, 0)
//
// These end at different positions, so }ll should NOT be a valid solution.
// =============================================================================

TEST_F(DebugSequenceTest, JjllFrom1_0EndsAtCorrectPosition) {
  Position result = simulateMotionsDefault({1, 0}, "jjll", a2_block_lines);
  expectPos(result, 3, 2, "jjll from (1,0)");
}

TEST_F(DebugSequenceTest, ParagraphMotionGoesToEmptyLine) {
  // Debug: print buffer content
  cout << "a2_block_lines has " << a2_block_lines.size() << " lines:\n";
  for (size_t i = 0; i < a2_block_lines.size(); i++) {
    cout << "  [" << i << "] \"" << a2_block_lines[i] << "\" (len=" << a2_block_lines[i].size() << ")\n";
  }

  // } from (1, 0) should go to the next blank line
  Position result = simulateMotionsDefault({1, 0}, "}", a2_block_lines);

  // If there's a blank line, } should find it. Otherwise it goes to last line.
  // Let's first verify what Vim would do
  cout << "} from (1,0) -> (" << result.line << ", " << result.col << ")\n";

  // Check if there's actually a blank line in the buffer
  bool hasBlankLine = false;
  int blankLineIdx = -1;
  for (size_t i = 2; i < a2_block_lines.size(); i++) {
    if (a2_block_lines[i].empty() ||
        a2_block_lines[i].find_first_not_of(" \t") == string::npos) {
      hasBlankLine = true;
      blankLineIdx = i;
      break;
    }
  }
  cout << "Has blank line after line 1: " << hasBlankLine << " at index " << blankLineIdx << "\n";

  if (hasBlankLine) {
    expectPos(result, blankLineIdx, 0, "} from (1,0) should go to blank line");
  } else {
    // No blank line - } goes to last character of last line
    int lastLine = (int)a2_block_lines.size() - 1;
    int lastCol = (int)a2_block_lines[lastLine].size() - 1;
    expectPos(result, lastLine, lastCol, "} from (1,0) goes to last char of last line when no blank");
  }
}

TEST_F(DebugSequenceTest, CloseBraceLLEndsAtEndOfLastLine) {
  // }ll from (1, 0): } goes to (3, 13), ll can't move further right
  Position result = simulateMotionsDefault({1, 0}, "}ll", a2_block_lines);
  expectPos(result, 3, 13, "}ll from (1,0) ends at (3,13) - already at line end");
}

TEST_F(DebugSequenceTest, AllResultsReachTargetPosition_From1_0) {
  Position start(1, 0);
  Position end(3, 2);

  vector<Result> results =
      runOptimizer(a2_block_lines, start, end, "jjll");

  cout << "Optimizer results for (1,0) -> (3,2):\n";
  for (const auto &r : results) {
    Position actual = simulateMotionsDefault(start, r.getSequenceString(), a2_block_lines);
    cout << "  " << r << " -> (" << actual.line << "," << actual.col << ")\n";
  }

  for (const auto &r : results) {
    Position actual = simulateMotionsDefault(start, r.getSequenceString(), a2_block_lines);
    EXPECT_EQ(actual.line, end.line)
        << "Sequence '" << makePrintable(r.getSequenceString()) << "' ends at wrong line: "
        << actual.line << " vs expected " << end.line;
    EXPECT_EQ(actual.col, end.col)
        << "Sequence '" << makePrintable(r.getSequenceString()) << "' ends at wrong col: "
        << actual.col << " vs expected " << end.col;
  }
}
