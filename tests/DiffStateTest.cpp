#include <gtest/gtest.h>

#include "Optimizer/DiffState.h"
#include "Utils/Lines.h"

// =============================================================================
// Character-Level Diff: Basic Cases
// =============================================================================

TEST(DiffStateTest, EndOfLineChange_OnlyChangedCharsInDiff) {
  Lines start = {"aaa bbb ccc xxx yyy zzz"};
  Lines end = {"aaa bbb ccc xxx yyy 000"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "zzz");
  EXPECT_EQ(diffs[0].insertedText, "000");
  EXPECT_EQ(diffs[0].posBegin.line, 0);
  EXPECT_EQ(diffs[0].posBegin.col, 20);
}

TEST(DiffStateTest, StartOfLineChange_OnlyChangedCharsInDiff) {
  Lines start = {"hello world"};
  Lines end = {"jello world"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "h");
  EXPECT_EQ(diffs[0].insertedText, "j");
  EXPECT_EQ(diffs[0].posBegin.line, 0);
  EXPECT_EQ(diffs[0].posBegin.col, 0);
}

TEST(DiffStateTest, MiddleOfLineChange_OnlyChangedCharsInDiff) {
  Lines start = {"the cat sat"};
  Lines end = {"the dog sat"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "cat");
  EXPECT_EQ(diffs[0].insertedText, "dog");
  EXPECT_EQ(diffs[0].posBegin.line, 0);
  EXPECT_EQ(diffs[0].posBegin.col, 4);
}

TEST(DiffStateTest, NoChange_NoDiffs) {
  Lines start = {"hello world"};
  Lines end = {"hello world"};

  auto diffs = Myers::calculate(start, end);

  EXPECT_EQ(diffs.size(), 0);
}

// =============================================================================
// Multiple Changes on Same Line
// =============================================================================

TEST(DiffStateTest, TwoSeparateChanges_TwoDiffs) {
  Lines start = {"aaa bbb ccc"};
  Lines end = {"xxx bbb yyy"};

  auto diffs = Myers::calculate(start, end);
  // "aaa" -> "xxx" and "ccc" -> "yyy"
  ASSERT_EQ(diffs.size(), 2);

  EXPECT_EQ(diffs[0].deletedText, "aaa");
  EXPECT_EQ(diffs[0].insertedText, "xxx");
  EXPECT_EQ(diffs[0].posBegin.col, 0);

  EXPECT_EQ(diffs[1].deletedText, "ccc");
  EXPECT_EQ(diffs[1].insertedText, "yyy");
  EXPECT_EQ(diffs[1].posBegin.col, 8);
}

TEST(DiffStateTest, ThreeChanges_ThreeDiffs) {
  Lines start = {"aa b cc d"};
  Lines end = {"11 b 22 d"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "aa");
  EXPECT_EQ(diffs[0].insertedText, "11");
  EXPECT_EQ(diffs[1].deletedText, "cc");
  EXPECT_EQ(diffs[1].insertedText, "22");
}

// =============================================================================
// Pure Insertions
// =============================================================================

TEST(DiffStateTest, InsertAtEnd) {
  Lines start = {"hello"};
  Lines end = {"hello world"};

  auto diffs = Myers::calculate(start, end);

  // Forward traceback should produce a single contiguous diff
  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "");
  EXPECT_EQ(diffs[0].insertedText, " world");
  EXPECT_TRUE(diffs[0].isPureInsertion());
}

TEST(DiffStateTest, InsertAtStart) {
  Lines start = {"world"};
  Lines end = {"hello world"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "");
  EXPECT_EQ(diffs[0].insertedText, "hello ");
  EXPECT_TRUE(diffs[0].isPureInsertion());
}

TEST(DiffStateTest, InsertInMiddle) {
  Lines start = {"helloworld"};
  Lines end = {"hello world"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "");
  EXPECT_EQ(diffs[0].insertedText, " ");
  EXPECT_TRUE(diffs[0].isPureInsertion());
}

// =============================================================================
// Pure Deletions
// =============================================================================

TEST(DiffStateTest, DeleteAtEnd) {
  Lines start = {"hello world"};
  Lines end = {"hello"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, " world");
  EXPECT_EQ(diffs[0].insertedText, "");
  EXPECT_TRUE(diffs[0].isPureDeletion());
}

TEST(DiffStateTest, DeleteAtStart) {
  Lines start = {"hello world"};
  Lines end = {"world"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "hello ");
  EXPECT_EQ(diffs[0].insertedText, "");
  EXPECT_TRUE(diffs[0].isPureDeletion());
}

TEST(DiffStateTest, DeleteInMiddle) {
  Lines start = {"hello world"};
  Lines end = {"helloworld"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, " ");
  EXPECT_EQ(diffs[0].insertedText, "");
  EXPECT_TRUE(diffs[0].isPureDeletion());
}

// =============================================================================
// Multi-Line Changes
// =============================================================================

TEST(DiffStateTest, MultiLine_SingleWordChange) {
  Lines start = {"aaa", "bbb", "ccc"};
  Lines end = {"aaa", "xxx", "ccc"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "bbb");
  EXPECT_EQ(diffs[0].insertedText, "xxx");
  EXPECT_EQ(diffs[0].posBegin.line, 1);
  EXPECT_EQ(diffs[0].posBegin.col, 0);
}

TEST(DiffStateTest, MultiLine_TwoChangesOnDifferentLines) {
  Lines start = {"aaa", "bbb", "ccc"};
  Lines end = {"xxx", "bbb", "yyy"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "aaa");
  EXPECT_EQ(diffs[0].insertedText, "xxx");
  EXPECT_EQ(diffs[0].posBegin.line, 0);

  EXPECT_EQ(diffs[1].deletedText, "ccc");
  EXPECT_EQ(diffs[1].insertedText, "yyy");
  EXPECT_EQ(diffs[1].posBegin.line, 2);
}

TEST(DiffStateTest, MultiLine_InsertNewLine) {
  Lines start = {"aaa", "ccc"};
  Lines end = {"aaa", "bbb", "ccc"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  // The diff should insert "\nbbb" or "bbb\n" depending on algorithm
  EXPECT_TRUE(diffs[0].isPureInsertion());
  // The inserted text should contain "bbb" and a newline
  EXPECT_NE(diffs[0].insertedText.find("bbb"), std::string::npos);
}

TEST(DiffStateTest, MultiLine_DeleteLine) {
  Lines start = {"aaa", "bbb", "ccc"};
  Lines end = {"aaa", "ccc"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_TRUE(diffs[0].isPureDeletion());
  // The deleted text should contain "bbb" and a newline
  EXPECT_NE(diffs[0].deletedText.find("bbb"), std::string::npos);
}

TEST(DiffStateTest, MultiLine_PartialLineChange) {
  // Note: "world" and "there" share 'r', so LCS splits this into 2 diffs.
  // For cases with no common chars in the changed region, we get 1 diff.
  Lines start = {"abc xyz", "foo bar"};
  Lines end = {"abc 123", "foo bar"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "xyz");
  EXPECT_EQ(diffs[0].insertedText, "123");
  EXPECT_EQ(diffs[0].posBegin.line, 0);
  EXPECT_EQ(diffs[0].posBegin.col, 4);
}

TEST(DiffStateTest, MultiLine_PartialLineChange_WithCommonChars) {
  // When the changed words share characters, LCS may produce multiple diffs
  // but apply still works correctly
  Lines start = {"hello world", "foo bar"};
  Lines end = {"hello there", "foo bar"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  // The diff might be split due to common 'r' in "world" and "there"
  EXPECT_GE(diffs.size(), 1u);
  EXPECT_EQ(result, end);
}

// =============================================================================
// Apply Diffs
// =============================================================================

TEST(DiffStateTest, ApplyDiff_SingleChange) {
  Lines start = {"hello world"};
  Lines end = {"hello there"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, ApplyDiff_MultipleChanges) {
  Lines start = {"the cat sat on the mat"};
  Lines end = {"the dog ran on the rug"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, ApplyDiff_MultiLine) {
  Lines start = {"aaa", "bbb", "ccc"};
  Lines end = {"xxx", "bbb", "yyy"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, ApplyDiff_InsertLine) {
  Lines start = {"aaa", "ccc"};
  Lines end = {"aaa", "bbb", "ccc"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, ApplyDiff_DeleteLine) {
  Lines start = {"aaa", "bbb", "ccc"};
  Lines end = {"aaa", "ccc"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, ApplyDiff_CompleteReplacement) {
  Lines start = {"hello"};
  Lines end = {"world"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(DiffStateTest, EmptyToNonEmpty) {
  Lines start = {""};
  Lines end = {"hello"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, NonEmptyToEmpty) {
  Lines start = {"hello"};
  Lines end = {""};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, SingleCharChange) {
  Lines start = {"a"};
  Lines end = {"b"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "a");
  EXPECT_EQ(diffs[0].insertedText, "b");

  Lines result = Myers::applyAllDiffState(diffs, start);
  EXPECT_EQ(result, end);
}

TEST(DiffStateTest, LongLineSmallChange) {
  // Simulate a realistic scenario: long line with small change at end
  std::string longPrefix(50, 'x');
  Lines start = {longPrefix + "aaa"};
  Lines end = {longPrefix + "bbb"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "aaa");
  EXPECT_EQ(diffs[0].insertedText, "bbb");
  EXPECT_EQ(diffs[0].posBegin.col, 50);

  Lines result = Myers::applyAllDiffState(diffs, start);
  EXPECT_EQ(result, end);
}

// =============================================================================
// Position Bounds
// =============================================================================

TEST(DiffStateTest, PosBeginPosEnd_SingleChar) {
  Lines start = {"abcde"};
  Lines end = {"abXde"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].posBegin.line, 0);
  EXPECT_EQ(diffs[0].posBegin.col, 2);
  EXPECT_EQ(diffs[0].posEnd.line, 0);
  EXPECT_EQ(diffs[0].posEnd.col, 2);  // Single char, posEnd == posBegin
}

TEST(DiffStateTest, PosBeginPosEnd_MultiChar) {
  Lines start = {"hello world"};
  Lines end = {"hello there"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].posBegin.line, 0);
  EXPECT_EQ(diffs[0].posBegin.col, 6);  // Start of "world"
  // posEnd should be at the last char of "world"
  EXPECT_EQ(diffs[0].posEnd.line, 0);
  EXPECT_EQ(diffs[0].posEnd.col, 10);  // Last char of "world"
}

TEST(DiffStateTest, PosBeginPosEnd_PureInsertion) {
  Lines start = {"hello"};
  Lines end = {"hello world"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  // For pure insertion, posBegin == posEnd (insertion point)
  EXPECT_EQ(diffs[0].posBegin, diffs[0].posEnd);
}

// =============================================================================
// DiffState Accessors
// =============================================================================

TEST(DiffStateTest, DeletedLinesAccessor) {
  Lines start = {"aaa bbb"};
  Lines end = {"aaa ccc"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  Lines deleted = diffs[0].deletedLines();
  EXPECT_EQ(deleted.size(), 1);
  EXPECT_EQ(deleted[0], "bbb");
}

TEST(DiffStateTest, InsertedLinesAccessor) {
  Lines start = {"aaa bbb"};
  Lines end = {"aaa ccc"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  Lines inserted = diffs[0].insertedLines();
  EXPECT_EQ(inserted.size(), 1);
  EXPECT_EQ(inserted[0], "ccc");
}

TEST(DiffStateTest, OrigCharCount) {
  Lines start = {"hello world"};
  Lines end = {"hello there"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].origCharCount(), 5);  // "world" = 5 chars
}

TEST(DiffStateTest, NewCharCount) {
  Lines start = {"hello world"};
  Lines end = {"hello there"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].newCharCount(), 5);  // "there" = 5 chars
}

// =============================================================================
// Search Space Reduction Verification
// =============================================================================

TEST(DiffStateTest, SearchSpaceReduction_LongLineSmallEdit) {
  // This test verifies the main benefit: small edits produce small diffs
  // even on long lines

  std::string line = "This is a very long line with lots of words and text that goes on and on end";
  Lines start = {line};

  // Change only the last word
  std::string endLine = line;
  endLine.replace(endLine.size() - 3, 3, "END");
  Lines end = {endLine};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  // The diff should be small, not the entire line
  EXPECT_LT(diffs[0].origCharCount(), 10);
  EXPECT_EQ(diffs[0].deletedText, "end");
  EXPECT_EQ(diffs[0].insertedText, "END");
}

// =============================================================================
// MIN_MATCH_LENGTH Threshold Behavior
// =============================================================================

TEST(DiffStateTest, MinMatchLength_ShortMatchMerged_1Char) {
  // "world" and "there" share only 'r' (1 char < MIN_MATCH_LENGTH=4)
  // Should produce 1 merged diff, not 2 separate diffs
  Lines start = {"world"};
  Lines end = {"there"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "world");
  EXPECT_EQ(diffs[0].insertedText, "there");
}

TEST(DiffStateTest, MinMatchLength_ShortMatchMerged_3Chars) {
  // "abcdef" and "xxcdexx" share "cde" (3 chars < MIN_MATCH_LENGTH=4)
  // Should merge into 1 diff
  Lines start = {"abcdef"};
  Lines end = {"xxcdexx"};

  auto diffs = Myers::calculate(start, end);

  // With MIN_MATCH_LENGTH=4, "cde" (3 chars) is too short to preserve
  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "abcdef");
  EXPECT_EQ(diffs[0].insertedText, "xxcdexx");
}

TEST(DiffStateTest, MinMatchLength_LongMatchPreserved_4Chars) {
  // "abcdefgh" and "xxcdefxx" share "cdef" (4 chars >= MIN_MATCH_LENGTH=4)
  // Should produce 2 separate diffs
  Lines start = {"abcdefgh"};
  Lines end = {"xxcdefxx"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "ab");
  EXPECT_EQ(diffs[0].insertedText, "xx");
  EXPECT_EQ(diffs[1].deletedText, "gh");
  EXPECT_EQ(diffs[1].insertedText, "xx");
}

TEST(DiffStateTest, MinMatchLength_LongMatchPreserved_6Chars) {
  // "migration" and "arbitrations" share "ration" (6 chars >= MIN_MATCH_LENGTH)
  // Should produce 2 separate diffs
  Lines start = {"migration"};
  Lines end = {"arbitrations"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "mig");
  EXPECT_EQ(diffs[0].insertedText, "arbit");
  // Second diff is the trailing 's' insertion
  EXPECT_EQ(diffs[1].deletedText, "");
  EXPECT_EQ(diffs[1].insertedText, "s");
}

TEST(DiffStateTest, MinMatchLength_RealisticRename_8CharMatch) {
  // "getUserData" and "fetchUserData" share "UserData" (8 chars)
  // Should produce 1 diff for the prefix change only
  Lines start = {"getUserData"};
  Lines end = {"fetchUserData"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "get");
  EXPECT_EQ(diffs[0].insertedText, "fetch");
}

TEST(DiffStateTest, MinMatchLength_HelloWorld_To_HelloThere) {
  // "hello world" -> "hello there" shares "hello " (6 chars) at start
  // and 'r' in the middle (but 'r' alone is < MIN_MATCH_LENGTH)
  // Should produce 1 diff for "world" -> "there"
  Lines start = {"hello world"};
  Lines end = {"hello there"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "world");
  EXPECT_EQ(diffs[0].insertedText, "there");
  EXPECT_EQ(diffs[0].posBegin.col, 6);
}

// =============================================================================
// Word Boundary Preservation
// =============================================================================

TEST(DiffStateTest, WordBoundary_SpaceSeparatedWords) {
  // "a b c" -> "d b e": " b " is only 3 chars but contains spaces
  // Should preserve " b " and produce 2 separate diffs
  Lines start = {"a b c"};
  Lines end = {"d b e"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "a");
  EXPECT_EQ(diffs[0].insertedText, "d");
  EXPECT_EQ(diffs[1].deletedText, "c");
  EXPECT_EQ(diffs[1].insertedText, "e");
}

TEST(DiffStateTest, WordBoundary_DotSeparatedTokens) {
  // "a.b.c" -> "d.b.e": ".b." is only 3 chars but contains dots
  // Should preserve ".b." and produce 2 separate diffs
  Lines start = {"a.b.c"};
  Lines end = {"d.b.e"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "a");
  EXPECT_EQ(diffs[0].insertedText, "d");
  EXPECT_EQ(diffs[1].deletedText, "c");
  EXPECT_EQ(diffs[1].insertedText, "e");
}

TEST(DiffStateTest, WordBoundary_UnderscoreNotBoundary) {
  // "_b_" is 3 chars with underscores, but underscore is NOT a word boundary
  // (it's part of identifiers in code), so should merge into 1 diff
  // Note: the common part must be in the MIDDLE, not at the end
  // (trailing matches are always preserved)
  Lines start = {"a_b_c"};
  Lines end = {"d_b_e"};

  auto diffs = Myers::calculate(start, end);

  // "_b_" (3 chars) should be merged because underscore is not a boundary
  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "a_b_c");
  EXPECT_EQ(diffs[0].insertedText, "d_b_e");
}

TEST(DiffStateTest, WordBoundary_CommaSeparated) {
  // "x,y,z" -> "a,y,b": ",y," contains commas
  Lines start = {"x,y,z"};
  Lines end = {"a,y,b"};

  auto diffs = Myers::calculate(start, end);

  ASSERT_EQ(diffs.size(), 2);
  EXPECT_EQ(diffs[0].deletedText, "x");
  EXPECT_EQ(diffs[0].insertedText, "a");
  EXPECT_EQ(diffs[1].deletedText, "z");
  EXPECT_EQ(diffs[1].insertedText, "b");
}

TEST(DiffStateTest, WordBoundary_Parentheses) {
  // "(foo)" -> "(bar)": "(" and ")" are preserved
  Lines start = {"(foo)"};
  Lines end = {"(bar)"};

  auto diffs = Myers::calculate(start, end);

  // The parens should be preserved, only the middle changes
  ASSERT_EQ(diffs.size(), 1);
  EXPECT_EQ(diffs[0].deletedText, "foo");
  EXPECT_EQ(diffs[0].insertedText, "bar");
}

TEST(DiffStateTest, WordBoundary_ApplyStillWorks) {
  // Verify that apply works correctly with word boundary preservation
  Lines start = {"a b c d e"};
  Lines end = {"x b y d z"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);
  // Should have 3 diffs: a->x, c->y, e->z
  EXPECT_EQ(diffs.size(), 3);
}

// =============================================================================
// Complex Cases (print for manual verification)
// =============================================================================

TEST(DiffStateTest, Complex_FunctionRename) {
  // Realistic: renaming a function call
  Lines start = {"result = getData(userId, options);"};
  Lines end = {"result = fetchData(userId, options);"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);

  // Print for manual verification
  std::cout << "\n[Complex_FunctionRename] \"" << start[0] << "\" -> \"" << end[0] << "\"" << std::endl;
  std::cout << "  Diffs: " << diffs.size() << std::endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    std::cout << "    [" << i << "] \"" << diffs[i].deletedText
              << "\" -> \"" << diffs[i].insertedText << "\"" << std::endl;
  }
}

TEST(DiffStateTest, Complex_MultipleWordChanges) {
  // Multiple words changed in a sentence
  Lines start = {"the quick brown fox jumps"};
  Lines end = {"the slow red fox leaps"};

  auto diffs = Myers::calculate(start, end);
  Lines result = Myers::applyAllDiffState(diffs, start);

  EXPECT_EQ(result, end);

  std::cout << "\n[Complex_MultipleWordChanges]" << std::endl;
  std::cout << "  \"" << start[0] << "\"" << std::endl;
  std::cout << "  \"" << end[0] << "\"" << std::endl;
  std::cout << "  Diffs: " << diffs.size() << std::endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    std::cout << "    [" << i << "] \"" << diffs[i].deletedText
              << "\" -> \"" << diffs[i].insertedText << "\"" << std::endl;
  }
}
