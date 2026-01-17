// tests/Operator/Words.cpp
//
// Tests for operator + word motion boundary crossing logic.
// Uses VimEndpointUtils for Position-based boundary prediction.

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
  const auto& motions = getAllWordMotions();

  int total = 0, passed = 0;

  for (int i = 0; i < NUM_BUFFERS; i++) {
    auto test = generateRandomBuffer(rng, 1);

    for (const auto& motion : motions) {
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
  int editStart = 1;
  int editEnd = 8;

  Position cursor(0, 4);  // On 'd' in "def"
  Position rightBoundary(0, editEnd + 1);  // 'h'

  // de from 'd' - should it cross to 'h'?
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, rightBoundary);

  // 'de' from 'd' goes to end of "def" (col 6), doesn't reach 'h' at col 9
  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, ManualExample_WordMotionCrossing) {
  // When content char continues into boundary char of same type
  Lines lines = {"abcdefgh"};

  Position cursor(0, 2);         // On 'c'
  Position rightBoundary(0, 5);  // 'f' - same word continues

  // de from 'c' - should go to end of word (col 7), crossing 'f' at col 5
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, rightBoundary);

  EXPECT_EQ(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, ManualExample_GapEdgeMotion) {
  // dw uses GapEdge - goes to start of next word
  Lines lines = {"abc   def"};

  Position cursor(0, 0);         // On 'a'
  Position rightBoundary(0, 6);  // 'd' - start of next word

  // dw from 'a' - should delete "abc   " and stop before 'd'
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::GapEdge, false, false, rightBoundary);

  // GapEdge stops at last whitespace before next word, which is col 5
  // Since 5 < 6, this should NOT cross the boundary
  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, ManualExample_BackwardMotion) {
  // db uses WordEdge backward
  Lines lines = {"abc def"};

  Position cursor(0, 4);        // On 'd' in "def"
  Position leftBoundary(0, 2);  // 'c' in "abc"

  // db from 'd' - goes to start of "def" (col 4), doesn't reach 'c' at col 2
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, false, EdgeType::WordEdge, false, false, leftBoundary);

  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

// =============================================================================
// Section 3: Edge Cases
// =============================================================================

TEST_F(WordsTest, EdgeCase_NoBoundary) {
  // When no boundary is set (invalid position), motion always succeeds
  Lines lines = {"hello world"};
  Position cursor(0, 0);

  // Use POSITION_OUTSIDE_BOUNDARY to indicate no boundary check
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, POSITION_OUTSIDE_BOUNDARY);

  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
  EXPECT_EQ(result.col, 4);  // End of "hello"
}

TEST_F(WordsTest, EdgeCase_BoundaryAtEndOfLine) {
  Lines lines = {"hello"};
  Position cursor(0, 0);
  Position rightBoundary(0, 4);  // 'o' at end

  // de from 'h' goes to col 4 ('o'), which equals boundary
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, rightBoundary);

  // Result at or past boundary = OUTSIDE_BOUNDARY
  EXPECT_EQ(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, EdgeCase_WORD_IncludesSymbols) {
  // WORD motions treat symbols as part of word
  Lines lines = {"abc...def ghi"};

  Position cursor(0, 0);         // On 'a'
  Position rightBoundary(0, 9);  // 'g' in "ghi"

  // dE from 'a' - goes to end of "abc...def" (col 8)
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, true, false, rightBoundary);

  // Col 8 < col 9, so should NOT cross
  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(WordsTest, EdgeCase_EmptyLine) {
  Lines lines = {"abc", "", "def"};

  Position cursor(0, 0);
  Position rightBoundary(2, 0);  // 'd' on line 2

  // de from 'a' goes to end of "abc" (col 2), doesn't reach line 2
  Position result = VimEndpointUtils::motionWordEndpoint(
      cursor, lines, true, EdgeType::WordEdge, false, false, rightBoundary);

  EXPECT_NE(result, POSITION_OUTSIDE_BOUNDARY);
}
