#include <gtest/gtest.h>

#include "Optimizer/DiffState.h"
#include "Utils/Lines.h"

// Helper: check diffs match expected {deleted, inserted} pairs
static void expectDiffs(
    const std::vector<DiffState> &diffs,
    std::initializer_list<std::pair<const char *, const char *>> expected) {
  ASSERT_EQ(diffs.size(), expected.size()) << "diff count mismatch";
  size_t i = 0;
  for (const auto &[del, ins] : expected) {
    EXPECT_EQ(diffs[i].deletedText, del) << "diff[" << i << "].deleted";
    EXPECT_EQ(diffs[i].insertedText, ins) << "diff[" << i << "].inserted";
    i++;
  }
}

// Helper: verify round-trip (calculate then apply)
static void expectRoundTrip(const Lines &start, const Lines &end) {
  auto diffs = Myers::calculate(start, end);
  EXPECT_EQ(Myers::applyAllDiffState(diffs, start), end);
}

// Helper: print diffs for manual verification
static void printDiffs(const char *name, const std::vector<DiffState> &diffs) {
  std::cout << "\n[" << name << "] " << diffs.size() << " diff(s)" << std::endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    std::cout << "  \"" << diffs[i].deletedText << "\" -> \""
              << diffs[i].insertedText << "\"" << std::endl;
  }
}

// =============================================================================
// Core Diff Behavior
// =============================================================================

TEST(DiffStateTest, NoChange_NoDiffs) {
  auto diffs = Myers::calculate({"hello"}, {"hello"});
  EXPECT_EQ(diffs.size(), 0);
}

TEST(DiffStateTest, Substitution_OnlyChangedChars) {
  auto diffs = Myers::calculate({"the cat sat"}, {"the dog sat"});
  expectDiffs(diffs, {{"cat", "dog"}});
}

TEST(DiffStateTest, MultipleDiffs_SameLine) {
  auto diffs = Myers::calculate({"aaa bbb ccc"}, {"xxx bbb yyy"});
  expectDiffs(diffs, {{"aaa", "xxx"}, {"ccc", "yyy"}});
}

TEST(DiffStateTest, PureInsertion) {
  auto diffs = Myers::calculate({"hello"}, {"hello world"});
  expectDiffs(diffs, {{"", " world"}});
  EXPECT_TRUE(diffs[0].isPureInsertion());
}

TEST(DiffStateTest, PureDeletion) {
  auto diffs = Myers::calculate({"hello world"}, {"hello"});
  expectDiffs(diffs, {{" world", ""}});
  EXPECT_TRUE(diffs[0].isPureDeletion());
}

// =============================================================================
// Multi-Line
// =============================================================================

TEST(DiffStateTest, MultiLine_ChangeOneLine) {
  auto diffs = Myers::calculate({"aaa", "bbb", "ccc"}, {"aaa", "xxx", "ccc"});
  expectDiffs(diffs, {{"bbb", "xxx"}});
  EXPECT_EQ(diffs[0].posBegin.line, 1);
}

TEST(DiffStateTest, MultiLine_ChangesOnDifferentLines) {
  auto diffs = Myers::calculate({"aaa", "bbb", "ccc"}, {"xxx", "bbb", "yyy"});
  expectDiffs(diffs, {{"aaa", "xxx"}, {"ccc", "yyy"}});
}

TEST(DiffStateTest, MultiLine_InsertLine) {
  auto diffs = Myers::calculate({"aaa", "ccc"}, {"aaa", "bbb", "ccc"});
  expectDiffs(diffs, {{"", "bbb\n"}});
}

TEST(DiffStateTest, MultiLine_DeleteLine) {
  auto diffs = Myers::calculate({"aaa", "bbb", "ccc"}, {"aaa", "ccc"});
  expectDiffs(diffs, {{"bbb\n", ""}});
}

// =============================================================================
// MIN_MATCH_LENGTH Threshold (default=4)
// =============================================================================

TEST(DiffStateTest, MinMatch_ShortCommonMerged) {
  // "cde" (3 chars) < MIN_MATCH_LENGTH=4, so merged into one diff
  auto diffs = Myers::calculate({"abcdef"}, {"xxcdexx"});
  expectDiffs(diffs, {{"abcdef", "xxcdexx"}});
}

TEST(DiffStateTest, MinMatch_LongCommonPreserved) {
  // "cdef" (4 chars) >= MIN_MATCH_LENGTH=4, so preserved as separator
  auto diffs = Myers::calculate({"abcdefgh"}, {"xxcdefxx"});
  expectDiffs(diffs, {{"ab", "xx"}, {"gh", "xx"}});
}

TEST(DiffStateTest, MinMatch_RealisticRename) {
  // "UserData" (8 chars) preserved, only prefix differs
  auto diffs = Myers::calculate({"getUserData"}, {"fetchUserData"});
  expectDiffs(diffs, {{"get", "fetch"}});
}

// =============================================================================
// Word Boundary Preservation (overrides MIN_MATCH_LENGTH)
// =============================================================================

TEST(DiffStateTest, WordBoundary_SpacePreserved) {
  // " b " is only 3 chars but contains spaces, so preserved
  auto diffs = Myers::calculate({"a b c"}, {"d b e"});
  expectDiffs(diffs, {{"a", "d"}, {"c", "e"}});
}

TEST(DiffStateTest, WordBoundary_PunctuationPreserved) {
  // ".b." and ",y," contain punctuation, so preserved
  auto diffs1 = Myers::calculate({"a.b.c"}, {"d.b.e"});
  expectDiffs(diffs1, {{"a", "d"}, {"c", "e"}});

  auto diffs2 = Myers::calculate({"x,y,z"}, {"a,y,b"});
  expectDiffs(diffs2, {{"x", "a"}, {"z", "b"}});
}

TEST(DiffStateTest, WordBoundary_UnderscoreNotBoundary) {
  // "_b_" has underscores but underscore is NOT a word boundary (for code)
  auto diffs = Myers::calculate({"a_b_c"}, {"d_b_e"});
  expectDiffs(diffs, {{"a_b_c", "d_b_e"}});
}

TEST(DiffStateTest, WordBoundary_ParensPreserveInner) {
  auto diffs = Myers::calculate({"(foo)"}, {"(bar)"});
  expectDiffs(diffs, {{"foo", "bar"}});
}

// =============================================================================
// Position Calculation
// =============================================================================

TEST(DiffStateTest, Position_SingleCharChange) {
  auto diffs = Myers::calculate({"abcde"}, {"abXde"});
  EXPECT_EQ(diffs[0].posBegin.col, 2);
  EXPECT_EQ(diffs[0].posEnd.col, 2); // Single char: posEnd == posBegin
}

TEST(DiffStateTest, Position_MultiCharChange) {
  auto diffs = Myers::calculate({"hello world"}, {"hello there"});
  EXPECT_EQ(diffs[0].posBegin.col, 6); // Start of "world"
  EXPECT_EQ(diffs[0].posEnd.col, 10);  // End of "world"
}

TEST(DiffStateTest, Position_PureInsertion) {
  auto diffs = Myers::calculate({"hello"}, {"hello world"});
  EXPECT_EQ(diffs[0].posBegin, diffs[0].posEnd); // Insertion point
}

// =============================================================================
// Accessors
// =============================================================================

TEST(DiffStateTest, Accessors) {
  auto diffs = Myers::calculate({"aaa bbb"}, {"aaa ccc"});
  ASSERT_EQ(diffs.size(), 1);

  EXPECT_EQ(diffs[0].deletedLines(), Lines({"bbb"}));
  EXPECT_EQ(diffs[0].insertedLines(), Lines({"ccc"}));
  EXPECT_EQ(diffs[0].origCharCount(), 3);
  EXPECT_EQ(diffs[0].newCharCount(), 3);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(DiffStateTest, EdgeCases) {
  // Empty to non-empty
  expectRoundTrip({""}, {"hello"});

  // Non-empty to empty
  expectRoundTrip({"hello"}, {""});

  // Single char
  auto diffs = Myers::calculate({"a"}, {"b"});
  expectDiffs(diffs, {{"a", "b"}});

  // Complete replacement
  expectRoundTrip({"hello"}, {"world"});
}

TEST(DiffStateTest, LongLineSmallChange) {
  std::string prefix(50, 'x');
  auto diffs = Myers::calculate({prefix + "aaa"}, {prefix + "bbb"});
  expectDiffs(diffs, {{"aaa", "bbb"}});
  EXPECT_EQ(diffs[0].posBegin.col, 50);
}

// =============================================================================
// Round-Trip (Apply) Verification
// =============================================================================

TEST(DiffStateTest, ApplyDiffs_Various) {
  // Single change
  expectRoundTrip({"hello world"}, {"hello there"});

  // Multiple changes
  expectRoundTrip({"the cat sat on the mat"}, {"the dog ran on the rug"});

  // Multi-line
  expectRoundTrip({"aaa", "bbb", "ccc"}, {"xxx", "bbb", "yyy"});

  // Insert/delete line
  expectRoundTrip({"aaa", "ccc"}, {"aaa", "bbb", "ccc"});
  expectRoundTrip({"aaa", "bbb", "ccc"}, {"aaa", "ccc"});

  // With word boundaries
  expectRoundTrip({"a b c d e"}, {"x b y d z"});
}

// =============================================================================
// Complex Cases (print for manual inspection)
// =============================================================================

TEST(DiffStateTest, Complex_FunctionRename) {
  auto diffs = Myers::calculate({"result = getData(userId, options);"},
                                {"result = fetchData(userId, options);"});
  printDiffs("FunctionRename", diffs);
  expectDiffs(diffs, {{"get", "fetch"}});
}

TEST(DiffStateTest, Complex_MultipleWordChanges) {
  Lines start = {"the quick brown fox jumps"};
  Lines end = {"the slow red fox leaps"};
  auto diffs = Myers::calculate(start, end);
  printDiffs("MultipleWordChanges", diffs);
  expectRoundTrip(start, end);
}


TEST(DiffStateTest, Complex_MainFunction) {
  Lines start = {
    "int main() {",
    "  int x = 0;",
    "  return x;",
    "}"
  };
  Lines end = {
    "#include <bits/stdc++.h>",
    "",
    "int main(int argc, char* argv) {",
    "  for(int i = 0; i < argc; i++) {",
    "    std::cout << argv[i] << \"\\n\";",
    "  }",
    "  return 0;",
    "}"
  };
  auto diffs = Myers::calculate(start, end);
  printDiffs("MainFunction", diffs);
  expectRoundTrip(start, end);
}


TEST(DiffStateTest, SimpleJoin) {
  Lines start = {
    "TEST(DiffStateTest, Complex_TestFunction) {",
    "  Lines start = {",
    "    aaa",
    "  };",
    "  Lines end = {",
    "    bbb",
    "  };",
    "}"
  };
  Lines end = {
    "TEST(DiffStateTest, SimpleJoin) {",
    "  Lines start = { aaa };",
    "  Lines end = { bbb };",
    "}"
  };
  auto diffs = Myers::calculate(start, end);
  printDiffs("SimpleJoin", diffs);
  expectRoundTrip(start, end);
}
