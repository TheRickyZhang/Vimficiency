// tests/Operator/Words.cpp
//
// Tests for operator + word motion boundary crossing logic.
// Uses VimCore for Position-based boundary prediction.

#include <gtest/gtest.h>

#include "TestHelpers.h"

#include <random>

using namespace std;

// =============================================================================
// WordsTest - Tests for operator + word motion boundary crossing logic
// =============================================================================

class WordsTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle_.reset(); }
  static unique_ptr<NeovimOracle> oracle_;

  mt19937 rng{42};
};

unique_ptr<NeovimOracle> WordsTest::oracle_;

// =============================================================================
// Section 1: Random Buffer Stress Tests
// =============================================================================

TEST_F(WordsTest, RandomBufferStress_SingleLine) {
  const int NUM_BUFFERS = 10;

  int total = 0, passed = 0;

  for (int i = 0; i < NUM_BUFFERS; i++) {
    auto test = generateRandomBuffer(rng, 1);

    for (const auto& motion : getAllWordMotions()) {
      total++;
      if (runRandomMotionTest(*oracle_, motion, test, true)) {
        passed++;
      }
    }
  }

  cerr << "\n=== Random Buffer (Single-Line): " << passed << "/" << total << " ===" << endl;
  EXPECT_EQ(passed, total);
}

TEST_F(WordsTest, RandomBufferStress_MultiLine) {
  const int NUM_BUFFERS = 10;
  const auto& motions = getAllWordMotions();

  int total = 0, passed = 0;

  for (int i = 0; i < NUM_BUFFERS; i++) {
    uniform_int_distribution<int> linesDist(2, 5);
    auto test = generateRandomBuffer(rng, linesDist(rng));

    for (const auto& motion : motions) {
      total++;
      if (runRandomMotionTest(*oracle_, motion, test, true)) {
        passed++;
      }
    }
  }

  cerr << "\n=== Random Buffer (Multi-Line): " << passed << "/" << total << " ===" << endl;
  EXPECT_EQ(passed, total);
}

// =============================================================================
// Section 2: Manual Examples
// =============================================================================

TEST_F(WordsTest, ManualExample_WordMotionForward) {
  // "abc def.gh i", edit region cols 1-8 ("bc def.g")
  Lines lines = {"abc def.gh i"};
  int editEnd = 8;

  Position cursor(0, 4);  // On 'd' in "def"
  // rightBoundary was at col 9 ('h'), so offset = lineLen - 9 = 12 - 9 = 3
  int boundaryOffset = static_cast<int>(lines[0].size()) - (editEnd + 1);

  // de from 'd' - should it cross to 'h'?
  // Single line, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, boundaryOffset, false);

  // 'de' from 'd' goes to end of "def" (col 6), doesn't reach 'h' at col 9
  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, ManualExample_WordMotionCrossing) {
  // When content char continues into boundary char of same type
  Lines lines = {"abcdefgh"};

  Position cursor(0, 2);  // On 'c'
  // rightBoundary was at col 5 ('f'), so offset = lineLen - 5 = 8 - 5 = 3
  int boundaryOffset = static_cast<int>(lines[0].size()) - 5;

  // de from 'c' - should go to end of word (col 7), crossing 'f' at col 5
  // Single line, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, boundaryOffset, false);

  EXPECT_EQ(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, ManualExample_GapEdgeMotion) {
  // dw uses GapEdge - goes to start of next word
  Lines lines = {"abc   def"};

  Position cursor(0, 0);  // On 'a'
  // rightBoundary was at col 6 ('d'), so offset = lineLen - 6 = 9 - 6 = 3
  int boundaryOffset = static_cast<int>(lines[0].size()) - 6;

  // dw from 'a' - should delete "abc   " and stop before 'd'
  // Single line, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::GapEdge, false, false, boundaryOffset, false);

  // GapEdge stops at last whitespace before next word, which is col 5
  // Since 5 < 6, this should NOT cross the boundary
  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, ManualExample_BackwardMotion) {
  // db uses WordEdge backward
  Lines lines = {"abc def"};

  Position cursor(0, 4);  // On 'd' in "def"
  // leftBoundary was at col 2 ('c'), so offset = 2 + 1 = 3 (protect cols 0,1,2)
  int boundaryOffset = 3;

  // db from 'd' - goes to start of "def" (col 4), doesn't reach 'c' at col 2
  // Single line, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, false, EdgeType::WordEdge, false, false, boundaryOffset, false);

  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

// =============================================================================
// Section 3: Edge Cases
// =============================================================================

TEST_F(WordsTest, EdgeCase_NoBoundary) {
  // When no boundary is set (offset <= 0), motion always succeeds
  Lines lines = {"hello world"};
  Position cursor(0, 0);

  // Use 0 to indicate no boundary check, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, 0, false);

  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
  EXPECT_EQ(result.col, 4);  // End of "hello"
}

TEST_F(WordsTest, EdgeCase_BoundaryAtEndOfLine) {
  Lines lines = {"hello"};
  Position cursor(0, 0);
  // rightBoundary was at col 4 ('o'), so offset = lineLen - 4 = 5 - 4 = 1
  int boundaryOffset = static_cast<int>(lines[0].size()) - 4;

  // de from 'h' goes to col 4 ('o'), which equals boundary
  // Single line, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, boundaryOffset, false);

  // Result at or past boundary = OUTSIDE_BOUNDARY
  EXPECT_EQ(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, EdgeCase_WORD_IncludesSymbols) {
  // WORD motions treat symbols as part of word
  Lines lines = {"abc...def ghi"};

  Position cursor(0, 0);  // On 'a'
  // rightBoundary was at col 9 ('g'), so offset = lineLen - 9 = 13 - 9 = 4
  int boundaryOffset = static_cast<int>(lines[0].size()) - 9;

  // dE from 'a' - goes to end of "abc...def" (col 8)
  // Single line, no lines outside
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, true, false, boundaryOffset, false);

  // Col 8 < col 9, so should NOT cross
  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, EdgeCase_EmptyLine) {
  Lines lines = {"abc", "", "def"};

  Position cursor(0, 0);
  // rightBoundary was at (2, 0), so offset on last line = lineLen - 0 = 3 - 0 = 3
  int boundaryOffset = static_cast<int>(lines[2].size()) - 0;

  // de from 'a' goes to end of "abc" (col 2), doesn't reach line 2
  // Multi-line, no lines outside (boundary is at line 2 which is last line)
  Position result = VimCore::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, boundaryOffset, false);

  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}
