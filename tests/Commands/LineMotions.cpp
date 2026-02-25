// tests/Commands/LineMotions.cpp
//
// Tests for line motion commands ($, 0, ^) using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="LineMotionsTest.*"

#include <gtest/gtest.h>

#include "Interpreter/MotionInterpreter.h"
#include "Types/NavContext.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class LineMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
    navContext = NavContext(80, 24);
  }

  static void TearDownTestSuite() {
    oracle.reset();
  }

  // Get expected result from Neovim
  static CursorPos neovimMotion(const Lines& lines, int startRow, int startCol, const string& keys) {
    auto result = oracle->simulate(lines, startRow, startCol, keys);
    return CursorPos(result.row, result.col);
  }
};

unique_ptr<NeovimOracle> LineMotionsTest::oracle;
NavContext LineMotionsTest::navContext(0, 0);

// =============================================================================
// Random Buffer Generation
// =============================================================================

struct RandomLineTest {
  Lines lines;
  int cursorLine;
  int cursorCol;
};

RandomLineTest generateRandomLineBuffer(int numLines) {
  RandomLineTest test;

  for (int i = 0; i < numLines; i++) {
    int len = RandomGen::range(0, 40);  // Include empty lines
    string line;

    // Random leading whitespace
    int spaces = RandomGen::range(0, 5);
    for (int j = 0; j < spaces && j < len; j++) {
      line += ' ';
    }

    // Rest is random chars
    for (int j = spaces; j < len; j++) {
      line += RandomGen::pick(CharPools::LETTERS);
    }
    test.lines.push_back(line);
  }

  // Random cursor position
  test.cursorLine = RandomGen::range(0, numLines - 1);
  int lineLen2 = static_cast<int>(test.lines[test.cursorLine].size());
  if (lineLen2 > 0) {
    test.cursorCol = RandomGen::range(0, lineLen2 - 1);
  } else {
    test.cursorCol = 0;
  }

  return test;
}

// =============================================================================
// Helper: Compare positions
// =============================================================================

bool linePositionsMatch(CursorPos ours, CursorPos neovim, const string& motion,
                        const RandomLineTest& test, bool verbose = false) {
  bool match = (ours.line == neovim.line && ours.col == neovim.col);

  if (!match && verbose) {
    cerr << "\n=== LINE MOTION TEST FAILURE ===" << endl;
    cerr << "Motion: " << motion << endl;
    cerr << "Start: (" << test.cursorLine << ", " << test.cursorCol << ")" << endl;
    cerr << "Our result: (" << ours.line << ", " << ours.col << ")" << endl;
    cerr << "Neovim result: (" << neovim.line << ", " << neovim.col << ")" << endl;
    cerr << "Buffer:" << endl;
    for (size_t i = 0; i < test.lines.size(); i++) {
      cerr << "  [" << i << "]: \"" << test.lines[i] << "\"" << endl;
    }
  }

  return match;
}

// =============================================================================
// Test helper: Random motion test
// =============================================================================

void testLineMotionRandom(NeovimOracle& oracle, const NavContext& navContext,
                          const string& motion, int numIterations, int numLines,
                          unsigned seed = 42) {
  RandomGen::seed(TEST_SEED(seed));
  int failures = 0;

  for (int i = 0; i < numIterations; i++) {
    auto test = generateRandomLineBuffer(numLines);

    // Our implementation
    CursorPos start(test.cursorLine, test.cursorCol);
    CursorPos ours = simulateMotions(start, motion, test.lines);

    // Neovim ground truth
    auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, motion);
    CursorPos neovim(result.row, result.col);

    if (!linePositionsMatch(ours, neovim, motion, test, failures < 5)) {
      failures++;
    }
  }

  EXPECT_EQ(failures, 0) << "Motion '" << motion << "' failed " << failures
                         << " out of " << numIterations << " random tests";
}

// =============================================================================
// Random Tests: $ (end of line)
// =============================================================================

TEST_F(LineMotionsTest, Dollar_RandomSingleLine) {
  testLineMotionRandom(*oracle, navContext, "$", 100, 1);
}

TEST_F(LineMotionsTest, Dollar_RandomMultiLine) {
  testLineMotionRandom(*oracle, navContext, "$", 100, 5);
}

// =============================================================================
// Random Tests: 0 (start of line)
// =============================================================================

TEST_F(LineMotionsTest, Zero_RandomSingleLine) {
  testLineMotionRandom(*oracle, navContext, "0", 100, 1);
}

TEST_F(LineMotionsTest, Zero_RandomMultiLine) {
  testLineMotionRandom(*oracle, navContext, "0", 100, 5);
}

// =============================================================================
// Random Tests: ^ (first non-blank)
// =============================================================================

TEST_F(LineMotionsTest, Caret_RandomSingleLine) {
  testLineMotionRandom(*oracle, navContext, "^", 100, 1);
}

TEST_F(LineMotionsTest, Caret_RandomMultiLine) {
  testLineMotionRandom(*oracle, navContext, "^", 100, 5);
}

// =============================================================================
// Manual Edge Case Tests
// =============================================================================

TEST_F(LineMotionsTest, Dollar_NormalLine) {
  Lines lines = {"hello world"};
  CursorPos result = simulateMotions({0, 0}, "$", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "$");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Dollar_EmptyLine) {
  Lines lines = {"", "content"};
  CursorPos result = simulateMotions({0, 0}, "$", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "$");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Dollar_AlreadyAtEnd) {
  Lines lines = {"hello"};
  CursorPos result = simulateMotions({0, 4}, "$", lines);
  CursorPos expected = neovimMotion(lines, 0, 4, "$");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Zero_FromMiddle) {
  Lines lines = {"hello world"};
  CursorPos result = simulateMotions({0, 6}, "0", lines);
  CursorPos expected = neovimMotion(lines, 0, 6, "0");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Zero_AlreadyAtStart) {
  Lines lines = {"hello"};
  CursorPos result = simulateMotions({0, 0}, "0", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "0");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Caret_WithLeadingSpaces) {
  Lines lines = {"   hello world"};
  CursorPos result = simulateMotions({0, 0}, "^", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "^");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Caret_NoLeadingSpaces) {
  Lines lines = {"hello world"};
  CursorPos result = simulateMotions({0, 5}, "^", lines);
  CursorPos expected = neovimMotion(lines, 0, 5, "^");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Caret_AllSpaces) {
  Lines lines = {"     "};
  CursorPos result = simulateMotions({0, 2}, "^", lines);
  CursorPos expected = neovimMotion(lines, 0, 2, "^");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(LineMotionsTest, Caret_EmptyLine) {
  Lines lines = {"", "content"};
  CursorPos result = simulateMotions({0, 0}, "^", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "^");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}
