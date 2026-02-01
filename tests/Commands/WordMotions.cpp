// tests/Commands/WordMotions.cpp
//
// Tests for word motion commands (w, e, b, ge, W, E, B, gE) using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="WordMotionsTest.*"

#include <gtest/gtest.h>

#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class WordMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext();

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
  }

  static void TearDownTestSuite() {
    oracle.reset();
  }

  // Get expected result from Neovim
  static Position neovimMotion(const Lines& lines, int startRow, int startCol, const string& keys) {
    auto result = oracle->simulate(lines, startRow, startCol, keys);
    return Position(result.row, result.col);
  }
};

unique_ptr<NeovimOracle> WordMotionsTest::oracle;

// =============================================================================
// Random Buffer Generation for Motion Testing
// =============================================================================

struct RandomMotionTest {
  Lines lines;
  int cursorLine;
  int cursorCol;
};

// Generate a random buffer with a valid cursor position
RandomMotionTest generateRandomMotionBuffer(int numLines) {
  RandomMotionTest test;
  test.lines = randomLines(numLines, 5, 40);
  Position pos = randomPosition(test.lines);
  test.cursorLine = pos.line;
  test.cursorCol = pos.col;
  return test;
}

// =============================================================================
// Helper: Compare positions
// =============================================================================

bool positionsMatch(Position ours, Position neovim, const string& motion,
                    const RandomMotionTest& test, bool verbose = false) {
  bool match = (ours.line == neovim.line && ours.col == neovim.col);

  if (!match && verbose) {
    cerr << "\n=== MOTION TEST FAILURE ===" << endl;
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
// Random Motion Tests
// =============================================================================

// Test a single motion with random buffers
void testMotionRandom(NeovimOracle& oracle, const string& motion, int numIterations, int numLines,
                      unsigned seed = 42) {
  RandomGen::seed(seed);
  int failures = 0;

  for (int i = 0; i < numIterations; i++) {
    auto test = generateRandomMotionBuffer(numLines);

    // Our implementation
    Position start(test.cursorLine, test.cursorCol);
    Position ours = simulateMotions(start, motion, test.lines);

    // Neovim ground truth
    auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, motion);
    Position neovim(result.row, result.col);

    if (!positionsMatch(ours, neovim, motion, test, failures < 5)) {
      failures++;
    }
  }

  EXPECT_EQ(failures, 0) << "Motion '" << motion << "' failed " << failures
                         << " out of " << numIterations << " random tests";
}

// =============================================================================
// Random Buffer Tests for Each Motion
// =============================================================================

TEST_F(WordMotionsTest, W_RandomSingleLine) {
  testMotionRandom(*oracle, "w", 100, 1);
}

TEST_F(WordMotionsTest, W_RandomMultiLine) {
  testMotionRandom(*oracle, "w", 100, 3);
}

TEST_F(WordMotionsTest, E_RandomSingleLine) {
  testMotionRandom(*oracle, "e", 100, 1);
}

TEST_F(WordMotionsTest, E_RandomMultiLine) {
  testMotionRandom(*oracle, "e", 100, 3);
}

TEST_F(WordMotionsTest, B_RandomSingleLine) {
  testMotionRandom(*oracle, "b", 100, 1);
}

TEST_F(WordMotionsTest, B_RandomMultiLine) {
  testMotionRandom(*oracle, "b", 100, 3);
}

TEST_F(WordMotionsTest, GE_RandomSingleLine) {
  testMotionRandom(*oracle, "ge", 100, 1);
}

TEST_F(WordMotionsTest, GE_RandomMultiLine) {
  testMotionRandom(*oracle, "ge", 100, 3);
}

// WORD variants - restart Neovim to prevent buffer exhaustion after 800 iterations
TEST_F(WordMotionsTest, BigW_RandomSingleLine) {
  oracle->restart();
  testMotionRandom(*oracle, "W", 100, 1);
}

TEST_F(WordMotionsTest, BigW_RandomMultiLine) {
  testMotionRandom(*oracle, "W", 100, 3);
}

TEST_F(WordMotionsTest, BigE_RandomSingleLine) {
  testMotionRandom(*oracle, "E", 100, 1);
}

TEST_F(WordMotionsTest, BigE_RandomMultiLine) {
  testMotionRandom(*oracle, "E", 100, 3);
}

TEST_F(WordMotionsTest, BigB_RandomSingleLine) {
  testMotionRandom(*oracle, "B", 100, 1);
}

TEST_F(WordMotionsTest, BigB_RandomMultiLine) {
  testMotionRandom(*oracle, "B", 100, 3);
}

TEST_F(WordMotionsTest, BigGE_RandomSingleLine) {
  testMotionRandom(*oracle, "gE", 100, 1);
}

TEST_F(WordMotionsTest, BigGE_RandomMultiLine) {
  testMotionRandom(*oracle, "gE", 100, 3);
}

// =============================================================================
// Manual Edge Case Tests - restart Neovim to prevent buffer exhaustion
// =============================================================================

TEST_F(WordMotionsTest, W_AtEndOfLine_CrossesToNextLine) {
  oracle->restart();
  Lines lines = {"hello world", "next line"};
  Position result = simulateMotions({0, 6}, "w", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, W_AtLastWord_StaysAtEnd) {
  Lines lines = {"hello world"};
  Position result = simulateMotions({0, 6}, "w", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, E_FromWordStart_GoesToWordEnd) {
  Lines lines = {"hello world"};
  Position result = simulateMotions({0, 0}, "e", lines);  // Start at 'h'
  Position expected = neovimMotion(lines, 0, 0, "e");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, E_FromWordEnd_GoesToNextWordEnd) {
  Lines lines = {"hello world"};
  Position result = simulateMotions({0, 4}, "e", lines);  // Start at 'o' (end of hello)
  Position expected = neovimMotion(lines, 0, 4, "e");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, B_FromWordStart_GoesToPreviousWordStart) {
  Lines lines = {"hello world"};
  Position result = simulateMotions({0, 6}, "b", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, B_FromMiddleOfWord_GoesToWordStart) {
  Lines lines = {"hello world"};
  Position result = simulateMotions({0, 8}, "b", lines);  // Start at 'r' in world
  Position expected = neovimMotion(lines, 0, 8, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, GE_FromWordStart_GoesToPreviousWordEnd) {
  Lines lines = {"hello world"};
  Position result = simulateMotions({0, 6}, "ge", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "ge");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, GE_FromWordEnd_GoesToPreviousWordEnd) {
  Lines lines = {"hello world test"};
  Position result = simulateMotions({0, 10}, "ge", lines);  // Start at 'd' (end of world)
  Position expected = neovimMotion(lines, 0, 10, "ge");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Symbol/keyword boundary tests
TEST_F(WordMotionsTest, W_KeywordToSymbol) {
  Lines lines = {"hello.world"};
  Position result = simulateMotions({0, 0}, "w", lines);
  Position expected = neovimMotion(lines, 0, 0, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, W_SymbolToKeyword) {
  Lines lines = {"...hello"};
  Position result = simulateMotions({0, 0}, "w", lines);
  Position expected = neovimMotion(lines, 0, 0, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, BigW_TreatsSymbolsAsWord) {
  Lines lines = {"hello...world"};
  Position result = simulateMotions({0, 0}, "W", lines);  // Should skip past "hello..." to "world"
  Position expected = neovimMotion(lines, 0, 0, "W");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Whitespace handling
TEST_F(WordMotionsTest, W_FromWhitespace) {
  Lines lines = {"hello   world"};
  Position result = simulateMotions({0, 6}, "w", lines);  // Start in whitespace
  Position expected = neovimMotion(lines, 0, 6, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, E_FromWhitespace) {
  Lines lines = {"hello   world"};
  Position result = simulateMotions({0, 6}, "e", lines);  // Start in whitespace
  Position expected = neovimMotion(lines, 0, 6, "e");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Multi-line crossing
TEST_F(WordMotionsTest, W_CrossesEmptyLine) {
  Lines lines = {"hello", "", "world"};
  Position result = simulateMotions({0, 0}, "w", lines);
  Position expected = neovimMotion(lines, 0, 0, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, B_CrossesEmptyLine) {
  Lines lines = {"hello", "", "world"};
  Position result = simulateMotions({2, 0}, "b", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 2, 0, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Edge cases at buffer boundaries
TEST_F(WordMotionsTest, W_AtBufferEnd) {
  Lines lines = {"hello"};
  Position result = simulateMotions({0, 4}, "w", lines);  // At last char
  Position expected = neovimMotion(lines, 0, 4, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, B_AtBufferStart) {
  Lines lines = {"hello"};
  Position result = simulateMotions({0, 0}, "b", lines);  // At first char
  Position expected = neovimMotion(lines, 0, 0, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(WordMotionsTest, GE_AtBufferStart) {
  Lines lines = {"hello"};
  Position result = simulateMotions({0, 0}, "ge", lines);  // At first char
  Position expected = neovimMotion(lines, 0, 0, "ge");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}
