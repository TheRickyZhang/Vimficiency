#include "Commands/MiscMotionsTestHelpers.h"
#include "Utils/NeovimOracle.h"

using namespace std;

namespace {

TEST_F(MiscMotionsTest, H_MovesLeft) {
  expectPos(simulateMovements({0, 5}, "h", a1_long_line), 0, 4);
  expectPos(simulateMovements({0, 5}, "hhh", a1_long_line), 0, 2);
}

TEST_F(MiscMotionsTest, H_StopsAtLineStart) {
  expectPos(simulateMovements({0, 0}, "h", a1_long_line), 0, 0);
  expectPos(simulateMovements({0, 2}, "hhhhh", a1_long_line), 0, 0);
}

TEST_F(MiscMotionsTest, L_MovesRight) {
  expectPos(simulateMovements({0, 0}, "l", a1_long_line), 0, 1);
  expectPos(simulateMovements({0, 0}, "lll", a1_long_line), 0, 3);
}

TEST_F(MiscMotionsTest, L_StopsAtLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(simulateMovements({0, lastCol}, "l", a1_long_line), 0, lastCol);
}

TEST_F(MiscMotionsTest, J_MovesDown) {
  expectPos(simulateMovements({0, 0}, "j", a2_block_lines), 1, 0);
  expectPos(simulateMovements({0, 0}, "jj", a2_block_lines), 2, 0);
  expectPos(simulateMovements({0, 0}, "jjj", a2_block_lines), 3, 0);
}

TEST_F(MiscMotionsTest, J_StopsAtLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  expectPos(simulateMovements({lastLine, 0}, "j", a2_block_lines), lastLine, 0);
  expectPos(simulateMovements({0, 0}, "jjjjjjjj", a2_block_lines), lastLine, 0);
}

TEST_F(MiscMotionsTest, K_MovesUp) {
  expectPos(simulateMovements({2, 0}, "k", a2_block_lines), 1, 0);
  expectPos(simulateMovements({3, 0}, "kk", a2_block_lines), 1, 0);
}

TEST_F(MiscMotionsTest, ParagraphMotionsTreatWhitespaceOnlyLinesAsContent) {
  Lines lines = {"alpha", " ", "beta", "gamma"};

  expectPos(simulateMovements({3, 4}, "{", lines), 0, 0);
  expectPos(simulateMovements({0, 0}, "}", lines), 3, 4);
}

TEST_F(MiscMotionsTest, K_StopsAtFirstLine) {
  expectPos(simulateMovements({0, 0}, "k", a2_block_lines), 0, 0);
  expectPos(simulateMovements({2, 0}, "kkkkk", a2_block_lines), 0, 0);
}

TEST_F(MiscMotionsTest, JK_PreservesColumn) {
  expectPos(simulateMovements({0, 5}, "j", a2_block_lines), 1, 5);
  expectPos(simulateMovements({0, 5}, "jj", a2_block_lines), 2, 5);
  expectPos(simulateMovements({2, 5}, "k", a2_block_lines), 1, 5);
}

TEST_F(MiscMotionsTest, JK_ClampsToShorterLine) {
  Lines lines = {"long line here", "short", "long line here"};
  expectPos(simulateMovements({0, 10}, "j", lines), 1, 4);
  expectPos(simulateMovements({0, 10}, "jk", lines), 0, 10);
}

TEST_F(MiscMotionsTest, JK_HandlesEmptyLines) {
  Lines lines = {"content", "", "content"};
  expectPos(simulateMovements({0, 5}, "j", lines), 1, 0);
  expectPos(simulateMovements({0, 5}, "jk", lines), 0, 5);
  expectPos(simulateMovements({1, 0}, "k", lines), 0, 0);
}

TEST_F(MiscMotionsTest, HorizontalNoOpPreservesTargetColumn) {
  Lines lines = {"abc def", "", "abc def"};
  expectPos(simulateMovements({0, 4}, "jhk", lines), 0, 4);
}

TEST_F(MiscMotionsTest, CaretOnWhitespaceOnlyLineMovesToLastColumn) {
  Lines lines = {"  "};
  expectPos(simulateMovements({0, 0}, "^", lines), 0, 1);
  expectPos(simulateMovements({0, 1}, "^", lines), 0, 1);
}

TEST_F(MiscMotionsTest, CountedBThenCaretOnWhitespaceOnlyLineMatchesVim) {
  Lines lines = {"  "};
  expectPos(simulateMovements({0, 1}, "6b2b^", lines), 0, 1);
}

TEST_F(MiscMotionsTest, GE_StopsOnEmptyLineBeforePreviousWordEnd) {
  Lines lines = {"abc def.", "", "  ghi jkl"};
  expectPos(simulateMovements({2, 2}, "b", lines), 1, 0);
  expectPos(simulateMovements({2, 2}, "2b", lines), 0, 7);
  expectPos(simulateMovements({2, 2}, "ge", lines), 1, 0);
  expectPos(simulateMovements({2, 2}, "2ge", lines), 0, 7);
  expectPos(simulateMovements({2, 2}, "gE", lines), 1, 0);
  expectPos(simulateMovements({2, 2}, "2gE", lines), 0, 7);
}

TEST_F(MiscMotionsTest, WOverConsecutiveEmptyLinesMatchesNeovim) {
  Lines lines = {"one", "", "", "two"};
  NeovimOracle oracle;

  SimulationResult expected = oracle.simulate(lines, 0, 0, "w");
  expectPos(simulateMovements({0, 0}, "w", lines), expected.row, expected.col);
}

// =============================================================================
// 2. FILE MOTIONS (gg, G)
// =============================================================================

TEST_F(MiscMotionsTest, GG_GoesToFirstLine) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  CursorPos p1 = simulateMovements({3, 5}, "gg", a2_block_lines);
  EXPECT_EQ(p1.line, 0);
  EXPECT_EQ(p1.col, 5);

  Lines lines = {"short", "longer line"};
  CursorPos p2 = simulateMovements({1, 8}, "gg", lines);
  EXPECT_EQ(p2.line, 0);
  EXPECT_EQ(p2.col, 4);
}

TEST_F(MiscMotionsTest, G_GoesToLastLine) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  int lastLine = a2_block_lines.size() - 1;
  CursorPos p = simulateMovements({0, 5}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
  EXPECT_EQ(p.col, 5);
}

TEST_F(MiscMotionsTest, CountedDollarPastEofFromEarlierLinePreservesEndOfLineTarget) {
  Lines lines = {"abc def ghi", "short", "tiny"};
  expectPos(simulateMovements({0, 2}, "5$k", lines), 1, 4);
}

TEST_F(MiscMotionsTest, CountedDollarPastEofFromLastLineIsNoOp) {
  Lines lines = {"abc def ghi", "aaa bbb ccc"};
  expectPos(simulateMovements({1, 0}, "2$jw", lines), 1, 4);
}

TEST_F(MiscMotionsTest, GG_G_RoundTrip) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  CursorPos start(2, 5);
  CursorPos atTop = simulateMovements(start, "gg", a2_block_lines);
  CursorPos atBottom = simulateMovements(atTop, "G", a2_block_lines);
  CursorPos backToTop = simulateMovements(atBottom, "gg", a2_block_lines);

  EXPECT_EQ(atTop.line, 0);
  EXPECT_EQ(atTop.col, 5);
  EXPECT_EQ(atBottom.line, (int)a2_block_lines.size() - 1);
  EXPECT_EQ(atBottom.col, 5);
  EXPECT_EQ(backToTop.line, 0);
  EXPECT_EQ(backToTop.col, 5);
}

// =============================================================================
// 3. CHARACTER FIND MOTIONS (f, F, t, T, ;, ,)
// =============================================================================

TEST_F(MiscMotionsTest, CharFind_TillRepeatSkipsAdjacentTarget) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "ta", lines), 0, 2, "ta lands before first match");
  expectPos(simulateMovements({0, 0}, "ta;", lines), 0, 5, "ta; lands before second match");
}

TEST_F(MiscMotionsTest, CharFind_TillReverseRepeat) {
  Lines lines = {". ,bcddcfaead feb."};
  expectPos(simulateMovements({0, 0}, "te;,,", lines), 0, 11, "te;,, matches Vim repeat direction");
}

TEST_F(MiscMotionsTest, CharFind_RepeatStateSurvivesInterveningMotion) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "fal;", lines), 0, 6, "repeat uses previous fa after l");
}

TEST_F(MiscMotionsTest, CharFind_CountedRepeat) {
  Lines lines = {"abcabcabcabc"};
  expectPos(simulateMovements({0, 0}, "fa2;", lines), 0, 9, "2; reaches second repeated match");
}

TEST_F(MiscMotionsTest, CharFind_CountedTillRepeatConsumesAdjacentTarget) {
  Lines forward = {"a c c c c"};
  expectPos(simulateMovements({0, 0}, "tc;", forward), 0, 3);
  expectPos(simulateMovements({0, 0}, "tc2;", forward), 0, 3);
  expectPos(simulateMovements({0, 0}, "tc3;", forward), 0, 5);

  Lines backward = {"c c c c a"};
  expectPos(simulateMovements({0, 8}, "Tc;", backward), 0, 5);
  expectPos(simulateMovements({0, 8}, "Tc2;", backward), 0, 5);
  expectPos(simulateMovements({0, 8}, "Tc3;", backward), 0, 3);
}

TEST_F(MiscMotionsTest, CharFind_CountedTillRepeatAfterInterveningMotion) {
  Lines lines = {
      "  ffffbbbb ee aabdfefa bbaaa ccbfbaac aaaaaa d cccaaaa. fbfebad aaaeeee eeeee dddd."};
  expectPos(simulateMovements({0, 50}, "lff2;TaEw4;", lines), 0, 52);
}

TEST_F(MiscMotionsTest, CharFind_SemicolonCanBeTarget) {
  Lines lines = {"a;b;c"};
  expectPos(simulateMovements({0, 0}, "f;;", lines), 0, 3, "first semicolon is target, second repeats");
}

TEST_F(MiscMotionsTest, CharFind_RepeatWithoutPriorFindIsNoOp) {
  Lines lines = {"abcabc"};
  expectPos(simulateMovements({0, 3}, ";", lines), 0, 3, "bare ; has no previous find");
  expectPos(simulateMovements({0, 3}, ",", lines), 0, 3, "bare , has no previous find");
}

TEST_F(MiscMotionsTest, CountedCharFind_IsAtomic) {
  Lines lines = {"abxba"};
  expectPos(simulateMovements({0, 0}, "2fb", lines), 0, 3, "2fb reaches second match");
  expectPos(simulateMovements({0, 0}, "3fb", lines), 0, 0, "3fb fails without partial movement");
  expectPos(simulateMovements({0, 0}, "2tb", lines), 0, 2, "2tb lands before second match");
  expectPos(simulateMovements({0, 0}, "3tb", lines), 0, 0, "3tb fails without partial movement");
}

TEST_F(MiscMotionsTest, EmptyLineNavigation) {
  Lines lines = {""};
  expectPos(simulateMovements({0, 0}, "l", lines), 0, 0);
  expectPos(simulateMovements({0, 0}, "h", lines), 0, 0);
}

TEST_F(MiscMotionsTest, SingleCharLine) {
  Lines lines = {"a"};
  expectPos(simulateMovements({0, 0}, "l", lines), 0, 0);
  expectPos(simulateMovements({0, 0}, "h", lines), 0, 0);
}

TEST_F(MiscMotionsTest, FirstAndLastPositions) {
  EXPECT_EQ(simulateMovements({0, 0}, "gg", a2_block_lines).line, 0);

  int lastLine = a2_block_lines.size() - 1;
  CursorPos p = simulateMovements({lastLine, 0}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
}

}  // namespace
