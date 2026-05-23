// tests/Operator/TextObjects.cpp
//
// Tests for operator + text object boundary crossing logic (diw, daw, etc.)
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TextObjectsTest.*"

#include <gtest/gtest.h>

#include <memory>

#include "Types/CharRange.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

// =============================================================================
// TextObjectsTest - Tests for text object boundary crossing
// =============================================================================

class TextObjectsTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle_.reset(); }
  static unique_ptr<NeovimOracle> oracle_;
};

unique_ptr<NeovimOracle> TextObjectsTest::oracle_;

static CharRange wordObjectRange(CursorPos cursor, const Lines& lines,
                                 bool isInner, bool isBigWord,
                                 int leftColOffset = 0,
                                 int rightColOffset = 0,
                                 bool hasLinesAbove = false,
                                 bool hasLinesBelow = false) {
  VimCore::WordBoundaryContext boundary;
  boundary.leftColOffset = leftColOffset;
  boundary.rightColOffset = rightColOffset;
  boundary.hasLinesAbove = hasLinesAbove;
  boundary.hasLinesBelow = hasLinesBelow;
  return VimCore::wordTextObjectRange(
      cursor, lines,
      isInner ? VimCore::WordTextObjectKind::Inner
              : VimCore::WordTextObjectKind::Around,
      isBigWord, boundary);
}

TEST_F(TextObjectsTest, Diw_InMiddleOfWord) {
  Lines lines = {"hello world"};
  auto result = oracle_->simulate(lines, 0, 2, "diw");  // Cursor on 'l' in hello
  // Should delete "hello", leaving " world"
  EXPECT_EQ(result.lines[0], " world");
}

TEST_F(TextObjectsTest, Diw_OnWhitespace) {
  Lines lines = {"hello   world"};
  auto result = oracle_->simulate(lines, 0, 6, "diw");  // Cursor in whitespace
  // Should delete the whitespace
  EXPECT_EQ(result.lines[0], "helloworld");
}

TEST_F(TextObjectsTest, Diw_OnTrailingWhitespaceBeforeNextLine) {
  Lines lines = {"abc ", " def"};
  auto result = oracle_->simulate(lines, 0, 3, "diw");

  EXPECT_EQ(result.lines, Lines({"abc", " def"}));
}

TEST_F(TextObjectsTest, Daw_WithTrailingWhitespace) {
  Lines lines = {"hello   world"};
  auto result = oracle_->simulate(lines, 0, 0, "daw");  // Cursor on 'h'
  // Should delete "hello   " (word + trailing whitespace)
  EXPECT_EQ(result.lines[0], "world");
}

TEST_F(TextObjectsTest, Daw_WithNoTrailingWhitespace) {
  Lines lines = {"hello"};
  auto result = oracle_->simulate(lines, 0, 0, "daw");  // Cursor on 'h', no trailing
  // Should delete entire word
  EXPECT_EQ(result.lines[0], "");
}

TEST_F(TextObjectsTest, Daw_LastWordWithLeadingWhitespace) {
  Lines lines = {"hello world"};
  auto result = oracle_->simulate(lines, 0, 6, "daw");  // Cursor on 'w' in world
  // Should delete " world" (leading whitespace + word, since no trailing)
  EXPECT_EQ(result.lines[0], "hello");
}

TEST_F(TextObjectsTest, DiW_OnSymbol) {
  Lines lines = {"hello... world"};  // Note: space before world
  auto result = oracle_->simulate(lines, 0, 0, "diW");  // Cursor on 'h'
  // bigWord treats "hello..." as one word (no whitespace in it)
  EXPECT_EQ(result.lines[0], " world");
}

TEST_F(TextObjectsTest, DaW_WithTrailingWhitespace) {
  Lines lines = {"hello...  world"};
  auto result = oracle_->simulate(lines, 0, 0, "daW");  // Cursor on 'h'
  // Should delete "hello...  "
  EXPECT_EQ(result.lines[0], "world");
}

TEST_F(TextObjectsTest, BracketRange_OnNestedClosingBracket) {
  Lines lines = {"(())"};

  CharRange inner = VimCore::bracketTextObjectRange(
      CursorPos(0, 3), lines, true, '(', ')');
  ASSERT_TRUE(inner.isValid());
  EXPECT_EQ(inner.begin, CursorPos(0, 1));
  EXPECT_EQ(inner.end, CursorPos(0, 3));

  CharRange around = VimCore::bracketTextObjectRange(
      CursorPos(0, 3), lines, false, '(', ')');
  ASSERT_TRUE(around.isValid());
  EXPECT_EQ(around.begin, CursorPos(0, 0));
  EXPECT_EQ(around.end, CursorPos(0, 4));
}

// =============================================================================
// Section 3: Boundary Checking Against VimCore
// =============================================================================

TEST_F(TextObjectsTest, TextObjectRange_InnerWord) {
  // Test that wordTextObjectRange correctly computes range for diw
  Lines lines = {"abc def ghi"};
  CursorPos cursor(0, 4);           // On 'd' in "def"
  // Old CursorPos boundary at col 3 → protect cols [0, 4), so leftColOffset = 4
  // Old CursorPos boundary at col 7 → protect cols [7, 11), so rightColOffset = 11 - 7 = 4
  int leftColOffset = 4;   // protect cols 0-3 (space before "def")
  int rightColOffset = 4;  // protect cols 7-10 (space after "def" and beyond)

  CharRange result = wordObjectRange(cursor, lines, true, false,
                                     leftColOffset, rightColOffset);

  // diw should NOT reach the boundaries (only selects "def" at cols 4-6)
  EXPECT_NE(result.begin, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TextObjectsTest, TextObjectRange_InnerWordWhitespaceStaysOnLine) {
  Lines lines = {"abc ", " def"};

  CharRange result = wordObjectRange(CursorPos(0, 3), lines, true, false);

  EXPECT_EQ(result.begin, CursorPos(0, 3));
  EXPECT_EQ(result.end, CursorPos(0, 4));
}

TEST_F(TextObjectsTest, TextObjectRange_AroundWord) {
  // Test that wordTextObjectRange correctly computes range for daw
  Lines lines = {"abc def ghi"};
  CursorPos cursor(0, 4);           // On 'd' in "def"
  // Old CursorPos boundary at col 2 → protect cols [0, 3), so leftColOffset = 3
  // Old CursorPos boundary at col 8 → protect cols [8, 11), so rightColOffset = 11 - 8 = 3
  int leftColOffset = 3;   // protect cols 0-2 ('c' in "abc" and before)
  int rightColOffset = 3;  // protect cols 8-10 ('g' in "ghi" and beyond)

  CharRange result = wordObjectRange(cursor, lines, false, false,
                                     leftColOffset, rightColOffset);

  // daw should NOT reach the boundaries (selects "def " at cols 4-7)
  EXPECT_NE(result.begin, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TextObjectsTest, TextObjectRange_CrossesBoundary) {
  // Test where text object DOES cross a boundary
  Lines lines = {"abc"};
  CursorPos cursor(0, 1);           // On 'b'
  // Old CursorPos boundary at col 0 → protect cols [0, 1), so leftColOffset = 1
  // Old CursorPos boundary at col 2 → protect cols [2, 3), so rightColOffset = 3 - 2 = 1
  int leftColOffset = 1;   // protect col 0 ('a' - adjacent to word)
  int rightColOffset = 1;  // protect col 2 ('c' - adjacent to word)

  CharRange result = wordObjectRange(cursor, lines, true, false,
                                     leftColOffset, rightColOffset);

  // diw on "abc" should reach both boundaries (selects entire word at cols 0-2)
  EXPECT_EQ(result.begin, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TextObjectsTest, TextObjectRange_NoBoundary) {
  // When no boundaries are set (offset = 0), text object always succeeds
  Lines lines = {"hello world"};
  CursorPos cursor(0, 0);

  // Use 0 to indicate no boundary check, no lines above/below
  CharRange result = wordObjectRange(cursor, lines, true, false);

  EXPECT_NE(result.begin, POSITION_OUTSIDE_BOUNDARY);
  EXPECT_EQ(result.begin.col, 0);
  EXPECT_EQ(result.end.col, 5);  // "hello" (exclusive end)
}
