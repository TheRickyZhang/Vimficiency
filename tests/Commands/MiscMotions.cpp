#include "Commands/MiscMotionsTestHelpers.h"

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

// =============================================================================
// 7. PROPERTY-BASED TESTS
// =============================================================================

TEST_F(MiscMotionsTest, Property_H_NeverIncreasesColumn) {
  for(int col = 0; col < 10; col++) {
    CursorPos result = simulateMovements({0, col}, "h", a1_long_line);
    EXPECT_LE(result.col, col) << "h from col " << col << " should not increase col";
    EXPECT_EQ(result.line, 0) << "h should not change line";
  }
}

TEST_F(MiscMotionsTest, Property_L_NeverDecreasesColumn) {
  for(int col = 0; col < 10; col++) {
    CursorPos result = simulateMovements({0, col}, "l", a1_long_line);
    EXPECT_GE(result.col, col) << "l from col " << col << " should not decrease col";
    EXPECT_EQ(result.line, 0) << "l should not change line";
  }
}

TEST_F(MiscMotionsTest, Property_J_NeverDecreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    CursorPos result = simulateMovements({line, 0}, "j", a2_block_lines);
    EXPECT_GE(result.line, line) << "j from line " << line << " should not decrease line";
  }
}

TEST_F(MiscMotionsTest, Property_K_NeverIncreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    CursorPos result = simulateMovements({line, 0}, "k", a2_block_lines);
    EXPECT_LE(result.line, line) << "k from line " << line << " should not increase line";
  }
}

TEST_F(MiscMotionsTest, Property_GG_AlwaysLine0) {
  auto lines = makeLines(20);
  for(int line = 0; line < (int)lines.size(); line++) {
    CursorPos result = simulateMovements({line, 0}, "gg", lines);
    EXPECT_EQ(result.line, 0) << "gg from line " << line << " should go to line 0";
  }
}

TEST_F(MiscMotionsTest, Property_G_AlwaysLastLine) {
  auto lines = makeLines(20);
  int lastLine = lines.size() - 1;
  for(int line = 0; line < (int)lines.size(); line++) {
    CursorPos result = simulateMovements({line, 0}, "G", lines);
    EXPECT_EQ(result.line, lastLine) << "G from line " << line << " should go to last line";
  }
}

}  // namespace
