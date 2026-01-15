// tests/Commands/BoundaryMotions.cpp
//
// Tests for word motion commands (w, e, b, ge, W, E, B, gE) using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Motion semantics from boundary-logic.md:
//   w  = (Forward, Next)   - move to START of next word
//   e  = (Forward, End)    - move to END of current/next word
//   b  = (Backward, Next)  - move to START of previous word
//   ge = (Backward, End)   - move to END of previous word

#include <gtest/gtest.h>

#include <random>

#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimMovementUtils.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class BoundaryMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
    navContext = NavContext(39, 19);
  }

  static void TearDownTestSuite() {
    oracle.reset();
  }

  // Apply motion using our implementation
  static Position applyMotion(Position start, const string& motion, const Lines& lines) {
    return simulateMotions(start, Mode::Normal, navContext, motion, lines).pos;
  }

  // Get expected result from Neovim
  static Position neovimMotion(const Lines& lines, int startRow, int startCol, const string& keys) {
    auto result = oracle->simulate(lines, startRow, startCol, keys);
    return Position(result.row, result.col);
  }
};

unique_ptr<NeovimOracle> BoundaryMotionsTest::oracle;
NavContext BoundaryMotionsTest::navContext(0, 0);

// =============================================================================
// Random Buffer Generation for Motion Testing
// =============================================================================

struct RandomMotionTest {
  Lines lines;
  int cursorLine;
  int cursorCol;
};

// Generate a random buffer with a valid cursor position
RandomMotionTest generateRandomMotionBuffer(mt19937& rng, int numLines) {
  RandomMotionTest test;

  // Character pools for different types
  const string keywords = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
  const string symbols = "!@#$%^&*()-=+[]{}|;:',.<>?/";
  const string whitespace = "    ";  // Just spaces for simplicity

  uniform_int_distribution<int> lineLen(5, 40);
  uniform_int_distribution<int> charTypeDist(0, 2);  // 0=keyword, 1=symbol, 2=space

  for (int i = 0; i < numLines; i++) {
    int len = lineLen(rng);
    string line;
    line.reserve(len);

    for (int j = 0; j < len; j++) {
      int charType = charTypeDist(rng);
      if (charType == 0) {
        line += keywords[rng() % keywords.size()];
      } else if (charType == 1) {
        line += symbols[rng() % symbols.size()];
      } else {
        line += ' ';
      }
    }
    test.lines.push_back(line);
  }

  // Pick a random cursor position
  uniform_int_distribution<int> lineDist(0, numLines - 1);
  test.cursorLine = lineDist(rng);
  int lineLen2 = test.lines[test.cursorLine].size();
  if (lineLen2 > 0) {
    uniform_int_distribution<int> colDist(0, lineLen2 - 1);
    test.cursorCol = colDist(rng);
  } else {
    test.cursorCol = 0;
  }

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
void testMotionRandom(NeovimOracle& oracle, const NavContext& navContext,
                      const string& motion, int numIterations, int numLines,
                      unsigned seed = 42) {
  mt19937 rng(seed);
  int failures = 0;

  for (int i = 0; i < numIterations; i++) {
    auto test = generateRandomMotionBuffer(rng, numLines);

    // Our implementation
    Position start(test.cursorLine, test.cursorCol);
    Position ours = simulateMotions(start, Mode::Normal, navContext, motion, test.lines).pos;

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

TEST_F(BoundaryMotionsTest, W_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "w", 100, 1);
}

TEST_F(BoundaryMotionsTest, W_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "w", 100, 3);
}

TEST_F(BoundaryMotionsTest, E_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "e", 100, 1);
}

TEST_F(BoundaryMotionsTest, E_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "e", 100, 3);
}

TEST_F(BoundaryMotionsTest, B_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "b", 100, 1);
}

TEST_F(BoundaryMotionsTest, B_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "b", 100, 3);
}

TEST_F(BoundaryMotionsTest, GE_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "ge", 100, 1);
}

TEST_F(BoundaryMotionsTest, GE_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "ge", 100, 3);
}

// WORD variants - restart Neovim to prevent buffer exhaustion after 800 iterations
TEST_F(BoundaryMotionsTest, BigW_RandomSingleLine) {
  oracle->restart();
  testMotionRandom(*oracle, navContext, "W", 100, 1);
}

TEST_F(BoundaryMotionsTest, BigW_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "W", 100, 3);
}

TEST_F(BoundaryMotionsTest, BigE_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "E", 100, 1);
}

TEST_F(BoundaryMotionsTest, BigE_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "E", 100, 3);
}

TEST_F(BoundaryMotionsTest, BigB_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "B", 100, 1);
}

TEST_F(BoundaryMotionsTest, BigB_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "B", 100, 3);
}

TEST_F(BoundaryMotionsTest, BigGE_RandomSingleLine) {
  testMotionRandom(*oracle, navContext, "gE", 100, 1);
}

TEST_F(BoundaryMotionsTest, BigGE_RandomMultiLine) {
  testMotionRandom(*oracle, navContext, "gE", 100, 3);
}

// =============================================================================
// Manual Edge Case Tests - restart Neovim to prevent buffer exhaustion
// =============================================================================

TEST_F(BoundaryMotionsTest, W_AtEndOfLine_CrossesToNextLine) {
  oracle->restart();
  Lines lines = {"hello world", "next line"};
  Position result = applyMotion({0, 6}, "w", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, W_AtLastWord_StaysAtEnd) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 6}, "w", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, E_FromWordStart_GoesToWordEnd) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 0}, "e", lines);  // Start at 'h'
  Position expected = neovimMotion(lines, 0, 0, "e");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, E_FromWordEnd_GoesToNextWordEnd) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 4}, "e", lines);  // Start at 'o' (end of hello)
  Position expected = neovimMotion(lines, 0, 4, "e");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, B_FromWordStart_GoesToPreviousWordStart) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 6}, "b", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, B_FromMiddleOfWord_GoesToWordStart) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 8}, "b", lines);  // Start at 'r' in world
  Position expected = neovimMotion(lines, 0, 8, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, GE_FromWordStart_GoesToPreviousWordEnd) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 6}, "ge", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 0, 6, "ge");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, GE_FromWordEnd_GoesToPreviousWordEnd) {
  Lines lines = {"hello world test"};
  Position result = applyMotion({0, 10}, "ge", lines);  // Start at 'd' (end of world)
  Position expected = neovimMotion(lines, 0, 10, "ge");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Symbol/keyword boundary tests
TEST_F(BoundaryMotionsTest, W_KeywordToSymbol) {
  Lines lines = {"hello.world"};
  Position result = applyMotion({0, 0}, "w", lines);
  Position expected = neovimMotion(lines, 0, 0, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, W_SymbolToKeyword) {
  Lines lines = {"...hello"};
  Position result = applyMotion({0, 0}, "w", lines);
  Position expected = neovimMotion(lines, 0, 0, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, BigW_TreatsSymbolsAsWord) {
  Lines lines = {"hello...world"};
  Position result = applyMotion({0, 0}, "W", lines);  // Should skip past "hello..." to "world"
  Position expected = neovimMotion(lines, 0, 0, "W");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Whitespace handling
TEST_F(BoundaryMotionsTest, W_FromWhitespace) {
  Lines lines = {"hello   world"};
  Position result = applyMotion({0, 6}, "w", lines);  // Start in whitespace
  Position expected = neovimMotion(lines, 0, 6, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, E_FromWhitespace) {
  Lines lines = {"hello   world"};
  Position result = applyMotion({0, 6}, "e", lines);  // Start in whitespace
  Position expected = neovimMotion(lines, 0, 6, "e");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Multi-line crossing
TEST_F(BoundaryMotionsTest, W_CrossesEmptyLine) {
  Lines lines = {"hello", "", "world"};
  Position result = applyMotion({0, 0}, "w", lines);
  Position expected = neovimMotion(lines, 0, 0, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, B_CrossesEmptyLine) {
  Lines lines = {"hello", "", "world"};
  Position result = applyMotion({2, 0}, "b", lines);  // Start at 'w' in world
  Position expected = neovimMotion(lines, 2, 0, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

// Edge cases at buffer boundaries
TEST_F(BoundaryMotionsTest, W_AtBufferEnd) {
  Lines lines = {"hello"};
  Position result = applyMotion({0, 4}, "w", lines);  // At last char
  Position expected = neovimMotion(lines, 0, 4, "w");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, B_AtBufferStart) {
  Lines lines = {"hello"};
  Position result = applyMotion({0, 0}, "b", lines);  // At first char
  Position expected = neovimMotion(lines, 0, 0, "b");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}

TEST_F(BoundaryMotionsTest, GE_AtBufferStart) {
  Lines lines = {"hello"};
  Position result = applyMotion({0, 0}, "ge", lines);  // At first char
  Position expected = neovimMotion(lines, 0, 0, "ge");
  EXPECT_EQ(result.line, expected.line);
  EXPECT_EQ(result.col, expected.col);
}
