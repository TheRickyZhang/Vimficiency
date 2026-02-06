// tests/Commands/MiscMotions.cpp
//
// Tests for miscellaneous motion commands not covered in dedicated test files:
// h, j, k, l, gg, G, f, F, t, T, ;, ,, <C-d>, <C-u>, <C-f>, <C-b>
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="MiscMotionsTest.*"
// Paragraph motions ({, }) are in ParagraphMotions.cpp
// Sentence motions ((, )) are in SentenceMotions.cpp

#include <gtest/gtest.h>
#include "Utils/TestUtils.h"
#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "VimCore/VimOptions.h"

using namespace std;

// =============================================================================
// MiscMotions Test Suite
// =============================================================================

class MiscMotionsTest : public ::testing::Test {
protected:
  static Lines a1_long_line;
  static Lines a2_block_lines;
  static NavContext navContext;

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    navContext = NavContext();
  }

  // Helper: assert position
  static void expectPos(Position actual, int line, int col, const string& msg = "") {
    EXPECT_EQ(actual.line, line) << msg << " (line)";
    EXPECT_EQ(actual.col, col) << msg << " (col)";
  }
};

// Static member definitions
Lines MiscMotionsTest::a1_long_line;
Lines MiscMotionsTest::a2_block_lines;
NavContext MiscMotionsTest::navContext(0, 0);

// =============================================================================
// 1. BASIC MOTIONS (h, j, k, l)
// =============================================================================

TEST_F(MiscMotionsTest, H_MovesLeft) {
  expectPos(simulateMotions({0, 5}, "h", a1_long_line), 0, 4);
  expectPos(simulateMotions({0, 5}, "hhh", a1_long_line), 0, 2);
}

TEST_F(MiscMotionsTest, H_StopsAtLineStart) {
  expectPos(simulateMotions({0, 0}, "h", a1_long_line), 0, 0);
  expectPos(simulateMotions({0, 2}, "hhhhh", a1_long_line), 0, 0);
}

TEST_F(MiscMotionsTest, L_MovesRight) {
  expectPos(simulateMotions({0, 0}, "l", a1_long_line), 0, 1);
  expectPos(simulateMotions({0, 0}, "lll", a1_long_line), 0, 3);
}

TEST_F(MiscMotionsTest, L_StopsAtLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(simulateMotions({0, lastCol}, "l", a1_long_line), 0, lastCol);
}

TEST_F(MiscMotionsTest, J_MovesDown) {
  expectPos(simulateMotions({0, 0}, "j", a2_block_lines), 1, 0);
  expectPos(simulateMotions({0, 0}, "jj", a2_block_lines), 2, 0);
  expectPos(simulateMotions({0, 0}, "jjj", a2_block_lines), 3, 0);
}

TEST_F(MiscMotionsTest, J_StopsAtLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  expectPos(simulateMotions({lastLine, 0}, "j", a2_block_lines), lastLine, 0);
  expectPos(simulateMotions({0, 0}, "jjjjjjjj", a2_block_lines), lastLine, 0);
}

TEST_F(MiscMotionsTest, K_MovesUp) {
  expectPos(simulateMotions({2, 0}, "k", a2_block_lines), 1, 0);
  expectPos(simulateMotions({3, 0}, "kk", a2_block_lines), 1, 0);
}

TEST_F(MiscMotionsTest, K_StopsAtFirstLine) {
  expectPos(simulateMotions({0, 0}, "k", a2_block_lines), 0, 0);
  expectPos(simulateMotions({2, 0}, "kkkkk", a2_block_lines), 0, 0);
}

TEST_F(MiscMotionsTest, JK_PreservesColumn) {
  expectPos(simulateMotions({0, 5}, "j", a2_block_lines), 1, 5);
  expectPos(simulateMotions({0, 5}, "jj", a2_block_lines), 2, 5);
  expectPos(simulateMotions({2, 5}, "k", a2_block_lines), 1, 5);
}

TEST_F(MiscMotionsTest, JK_ClampsToShorterLine) {
  Lines lines = {"long line here", "short", "long line here"};
  expectPos(simulateMotions({0, 10}, "j", lines), 1, 4);
  expectPos(simulateMotions({0, 10}, "jk", lines), 0, 10);
}

TEST_F(MiscMotionsTest, JK_HandlesEmptyLines) {
  Lines lines = {"content", "", "content"};
  expectPos(simulateMotions({0, 5}, "j", lines), 1, 0);
  expectPos(simulateMotions({0, 5}, "jk", lines), 0, 5);
  expectPos(simulateMotions({1, 0}, "k", lines), 0, 0);
}

// =============================================================================
// 2. FILE MOTIONS (gg, G)
// =============================================================================

TEST_F(MiscMotionsTest, GG_GoesToFirstLine) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  Position p1 = simulateMotions({3, 5}, "gg", a2_block_lines);
  EXPECT_EQ(p1.line, 0);
  EXPECT_EQ(p1.col, 5);

  Lines lines = {"short", "longer line"};
  Position p2 = simulateMotions({1, 8}, "gg", lines);
  EXPECT_EQ(p2.line, 0);
  EXPECT_EQ(p2.col, 4);
}

TEST_F(MiscMotionsTest, G_GoesToLastLine) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  int lastLine = a2_block_lines.size() - 1;
  Position p = simulateMotions({0, 5}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
  EXPECT_EQ(p.col, 5);
}

TEST_F(MiscMotionsTest, GG_G_RoundTrip) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  Position start(2, 5);
  Position atTop = simulateMotions(start, "gg", a2_block_lines);
  Position atBottom = simulateMotions(atTop, "G", a2_block_lines);
  Position backToTop = simulateMotions(atBottom, "gg", a2_block_lines);

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

TEST_F(MiscMotionsTest, F_FindForward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 0}, "fc", lines), 0, 2, "f from 0 to 'c'");
  expectPos(simulateMotions({0, 0}, "fj", lines), 0, 9, "f from 0 to 'j'");
  expectPos(simulateMotions({0, 3}, "fg", lines), 0, 6, "f from 3 to 'g'");
}

TEST_F(MiscMotionsTest, F_FindForward_NotFound) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 0}, "fz", lines), 0, 0, "f to nonexistent char");
  expectPos(simulateMotions({0, 5}, "fa", lines), 0, 5, "f forward to char behind cursor");
}

TEST_F(MiscMotionsTest, F_FindBackward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 9}, "Fc", lines), 0, 2, "F from 9 to 'c'");
  expectPos(simulateMotions({0, 9}, "Fa", lines), 0, 0, "F from 9 to 'a'");
  expectPos(simulateMotions({0, 6}, "Fd", lines), 0, 3, "F from 6 to 'd'");
}

TEST_F(MiscMotionsTest, F_FindBackward_NotFound) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 9}, "Fz", lines), 0, 9, "F to nonexistent char");
  expectPos(simulateMotions({0, 3}, "Fj", lines), 0, 3, "F backward to char ahead of cursor");
}

TEST_F(MiscMotionsTest, T_TillForward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 0}, "tc", lines), 0, 1, "t from 0 to before 'c'");
  expectPos(simulateMotions({0, 0}, "tj", lines), 0, 8, "t from 0 to before 'j'");
  expectPos(simulateMotions({0, 3}, "tg", lines), 0, 5, "t from 3 to before 'g'");
}

TEST_F(MiscMotionsTest, T_TillForward_AdjacentChar) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 0}, "tb", lines), 0, 0, "t to adjacent char stays");
}

TEST_F(MiscMotionsTest, T_TillBackward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 9}, "Tc", lines), 0, 3, "T from 9 to after 'c'");
  expectPos(simulateMotions({0, 9}, "Ta", lines), 0, 1, "T from 9 to after 'a'");
}

TEST_F(MiscMotionsTest, T_TillBackward_AdjacentChar) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMotions({0, 5}, "Te", lines), 0, 5, "T to adjacent char stays");
}

TEST_F(MiscMotionsTest, CharFind_WithSemicolonRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMotions({0, 0}, "fa", lines), 0, 3, "fa from 0");
  expectPos(simulateMotions({0, 0}, "fa;", lines), 0, 6, "fa; from 0");
  expectPos(simulateMotions({0, 8}, "Fa", lines), 0, 6, "Fa from 8");
  expectPos(simulateMotions({0, 8}, "Fa;", lines), 0, 3, "Fa; from 8");
  expectPos(simulateMotions({0, 8}, "Fa;;", lines), 0, 0, "Fa;; from 8");
}

TEST_F(MiscMotionsTest, CharFind_WithCommaRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMotions({0, 0}, "fa,", lines), 0, 0, "fa, from 0 - back to start");
  expectPos(simulateMotions({0, 0}, "fa;,", lines), 0, 3, "fa;, from 0 - forward twice, back once");
  expectPos(simulateMotions({0, 8}, "Fa,", lines), 0, 6, "Fa, from 8 - stays at 6");
}

TEST_F(MiscMotionsTest, CharFind_MixedRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMotions({0, 0}, "fa;,;", lines), 0, 6, "fa;,; complex repeat");
}

TEST_F(MiscMotionsTest, CharFind_TillWithRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMotions({0, 0}, "ta", lines), 0, 2, "ta from 0 - before first 'a' at 3");
  expectPos(simulateMotions({0, 0}, "ta;", lines), 0, 2, "ta; from 0 - stuck at 2");
  expectPos(simulateMotions({0, 0}, "fa;", lines), 0, 6, "fa; from 0 advances properly");
}

TEST_F(MiscMotionsTest, CharFind_SpaceAsTarget) {
  Lines lines = {"abc def ghi"};
  expectPos(simulateMotions({0, 0}, "f ", lines), 0, 3, "f<space> from 0");
  expectPos(simulateMotions({0, 0}, "f ;", lines), 0, 7, "f<space>; from 0");
  expectPos(simulateMotions({0, 0}, "t ", lines), 0, 2, "t<space> from 0");
  expectPos(simulateMotions({0, 10}, "F ", lines), 0, 7, "F<space> from end");
  expectPos(simulateMotions({0, 10}, "F ;", lines), 0, 3, "F<space>; from end");
}

TEST_F(MiscMotionsTest, CharFind_MultipleOccurrences) {
  Lines lines = {"aaaaaa"};
  expectPos(simulateMotions({0, 0}, "fa", lines), 0, 1, "fa in all-a line");
  expectPos(simulateMotions({0, 0}, "fa;", lines), 0, 2, "fa; in all-a line");
  expectPos(simulateMotions({0, 0}, "fa;;", lines), 0, 3, "fa;; in all-a line");
  expectPos(simulateMotions({0, 0}, "fa;;;", lines), 0, 4, "fa;;; in all-a line");
}

TEST_F(MiscMotionsTest, CharFind_CombinedWithOtherMotions) {
  Lines lines = {"abcdefghij", "0123456789"};
  expectPos(simulateMotions({0, 0}, "jfc", lines), 1, 0, "j then fc (no 'c' in line 1)");
  expectPos(simulateMotions({0, 0}, "fcj", lines), 1, 2, "fc then j");
}

TEST_F(MiscMotionsTest, CharFind_AtLineEnd) {
  Lines lines = {"abcdef"};
  expectPos(simulateMotions({0, 5}, "fa", lines), 0, 5, "f from end - target behind");
  expectPos(simulateMotions({0, 0}, "Ff", lines), 0, 0, "F from start - target ahead");
}

TEST_F(MiscMotionsTest, CharFind_OnTargetChar) {
  Lines lines = {"abcabc"};
  expectPos(simulateMotions({0, 0}, "fa", lines), 0, 3, "fa when on 'a' finds next 'a'");
  expectPos(simulateMotions({0, 3}, "Fa", lines), 0, 0, "Fa when on 'a' finds previous 'a'");
}

// =============================================================================
// 4. SCROLL MOTIONS (<C-d>, <C-u>, <C-f>, <C-b>)
// =============================================================================

static Position simulateWithNav(Position start, const string& motion,
                                const Lines& lines, NavContext nav) {
  return simulateMotions(start, motion, lines, nav);
}

static Lines makeLines(int count) {
  Lines lines;
  for (int i = 0; i < count; i++) {
    lines.push_back("line" + to_string(i));
  }
  return lines;
}

TEST_F(MiscMotionsTest, CtrlD_Basic) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav), 20, 0, "C-d from line 0");
  expectPos(simulateWithNav({10, 0}, "<C-d>", lines, nav), 30, 0, "C-d from line 10");
  expectPos(simulateWithNav({50, 0}, "<C-d>", lines, nav), 70, 0, "C-d from line 50");
}

TEST_F(MiscMotionsTest, CtrlD_DifferentScrollAmounts) {
  auto lines = makeLines(100);

  NavContext nav10(20, 10);
  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav10), 10, 0, "C-d with scroll=10");

  NavContext nav5(10, 5);
  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav5), 5, 0, "C-d with scroll=5");

  NavContext nav30(60, 30);
  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav30), 30, 0, "C-d with scroll=30");
}

TEST_F(MiscMotionsTest, CtrlD_StopsAtEndOfFile) {
  auto lines = makeLines(50);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({40, 0}, "<C-d>", lines, nav), 49, 0, "C-d near end clamps to last line");
  expectPos(simulateWithNav({49, 0}, "<C-d>", lines, nav), 49, 0, "C-d at last line stays");
  expectPos(simulateWithNav({45, 0}, "<C-d>", lines, nav), 49, 0, "C-d partial scroll at end");
}

TEST_F(MiscMotionsTest, CtrlD_PreservesColumn) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 3}, "<C-d>", lines, nav), 20, 3, "C-d preserves column");
}

TEST_F(MiscMotionsTest, CtrlD_ClampsColumnOnShorterLine) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  Lines lines = {
    "long line here",
    "short",
    "long line here",
  };
  NavContext nav(3, 1);

  Position result = simulateWithNav({0, 10}, "<C-d>", lines, nav);
  EXPECT_EQ(result.line, 1);
  EXPECT_EQ(result.col, 4) << "Column should clamp to end of shorter line";
}

TEST_F(MiscMotionsTest, CtrlU_Basic) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({50, 0}, "<C-u>", lines, nav), 30, 0, "C-u from line 50");
  expectPos(simulateWithNav({30, 0}, "<C-u>", lines, nav), 10, 0, "C-u from line 30");
  expectPos(simulateWithNav({99, 0}, "<C-u>", lines, nav), 79, 0, "C-u from last line");
}

TEST_F(MiscMotionsTest, CtrlU_StopsAtTopOfFile) {
  auto lines = makeLines(50);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({10, 0}, "<C-u>", lines, nav), 0, 0, "C-u near top clamps to line 0");
  expectPos(simulateWithNav({0, 0}, "<C-u>", lines, nav), 0, 0, "C-u at line 0 stays");
  expectPos(simulateWithNav({5, 0}, "<C-u>", lines, nav), 0, 0, "C-u partial scroll at top");
}

TEST_F(MiscMotionsTest, CtrlU_PreservesColumn) {
  if constexpr (VimOptions::startOfLine()) GTEST_SKIP() << "Neovim-only (column preservation)";
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({50, 3}, "<C-u>", lines, nav), 30, 3, "C-u preserves column");
}

TEST_F(MiscMotionsTest, CtrlF_Basic) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav), 38, 0, "C-f from line 0");
  expectPos(simulateWithNav({10, 0}, "<C-f>", lines, nav), 48, 0, "C-f from line 10");
}

TEST_F(MiscMotionsTest, CtrlF_DifferentWindowHeights) {
  auto lines = makeLines(100);

  NavContext nav20(20, 10);
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav20), 18, 0, "C-f with window=20");

  NavContext nav50(50, 25);
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav50), 48, 0, "C-f with window=50");
}

TEST_F(MiscMotionsTest, CtrlF_StopsAtEndOfFile) {
  auto lines = makeLines(50);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({20, 0}, "<C-f>", lines, nav), 49, 0, "C-f near end clamps");
  expectPos(simulateWithNav({49, 0}, "<C-f>", lines, nav), 49, 0, "C-f at last line stays");
}

TEST_F(MiscMotionsTest, CtrlB_Basic) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({50, 0}, "<C-b>", lines, nav), 12, 0, "C-b from line 50");
  expectPos(simulateWithNav({99, 0}, "<C-b>", lines, nav), 61, 0, "C-b from last line");
}

TEST_F(MiscMotionsTest, CtrlB_DifferentWindowHeights) {
  auto lines = makeLines(100);

  NavContext nav20(20, 10);
  expectPos(simulateWithNav({50, 0}, "<C-b>", lines, nav20), 32, 0, "C-b with window=20");

  NavContext nav50(50, 25);
  expectPos(simulateWithNav({50, 0}, "<C-b>", lines, nav50), 2, 0, "C-b with window=50");
}

TEST_F(MiscMotionsTest, CtrlB_StopsAtTopOfFile) {
  auto lines = makeLines(50);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({20, 0}, "<C-b>", lines, nav), 0, 0, "C-b near top clamps");
  expectPos(simulateWithNav({0, 0}, "<C-b>", lines, nav), 0, 0, "C-b at line 0 stays");
}

TEST_F(MiscMotionsTest, Scroll_RoundTrip) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  Position p1 = simulateWithNav({30, 0}, "<C-d>", lines, nav);
  Position p2 = simulateWithNav(p1, "<C-u>", lines, nav);
  EXPECT_EQ(p2.line, 30) << "C-d then C-u should return to original";

  Position p3 = simulateWithNav({30, 0}, "<C-f>", lines, nav);
  Position p4 = simulateWithNav(p3, "<C-b>", lines, nav);
  EXPECT_EQ(p4.line, 30) << "C-f then C-b should return to original";
}

TEST_F(MiscMotionsTest, Scroll_MultipleScrolls) {
  auto lines = makeLines(100);
  NavContext nav(40, 10);

  Position p = {0, 0};
  p = simulateWithNav(p, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 10);
  p = simulateWithNav(p, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 20);
  p = simulateWithNav(p, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 30);
}

TEST_F(MiscMotionsTest, Scroll_SmallFile) {
  auto lines = makeLines(5);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav), 4, 0, "C-d in small file");
  expectPos(simulateWithNav({4, 0}, "<C-u>", lines, nav), 0, 0, "C-u in small file");
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav), 4, 0, "C-f in small file");
  expectPos(simulateWithNav({4, 0}, "<C-b>", lines, nav), 0, 0, "C-b in small file");
}

TEST_F(MiscMotionsTest, Scroll_SingleLine) {
  Lines lines = {"only line"};
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav), 0, 0, "C-d single line");
  expectPos(simulateWithNav({0, 0}, "<C-u>", lines, nav), 0, 0, "C-u single line");
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav), 0, 0, "C-f single line");
  expectPos(simulateWithNav({0, 0}, "<C-b>", lines, nav), 0, 0, "C-b single line");
}

TEST_F(MiscMotionsTest, Scroll_ZeroScrollAmount) {
  auto lines = makeLines(50);
  NavContext nav(40, 0);

  Position p = simulateWithNav({25, 0}, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-d with scroll=0 should not move";

  p = simulateWithNav({25, 0}, "<C-u>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-u with scroll=0 should not move";
}

TEST_F(MiscMotionsTest, Scroll_WindowHeightTwo) {
  auto lines = makeLines(50);
  NavContext nav(2, 1);

  Position p = simulateWithNav({25, 0}, "<C-f>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-f with window=2 should not move";

  p = simulateWithNav({25, 0}, "<C-b>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-b with window=2 should not move";
}

// =============================================================================
// 5. COUNT PREFIXES (for motions not in other files)
// =============================================================================

TEST_F(MiscMotionsTest, Count_BasicHJKL) {
  expectPos(simulateMotions({0, 10}, "3h", a1_long_line), 0, 7, "3h moves left 3");
  expectPos(simulateMotions({0, 0}, "5l", a1_long_line), 0, 5, "5l moves right 5");
  expectPos(simulateMotions({0, 0}, "2j", a2_block_lines), 2, 0, "2j moves down 2");
  expectPos(simulateMotions({3, 0}, "2k", a2_block_lines), 1, 0, "2k moves up 2");
}

TEST_F(MiscMotionsTest, Count_LargeCount) {
  auto lines = makeLines(100);
  expectPos(simulateMotions({0, 0}, "50j", lines), 50, 0, "50j");
  expectPos(simulateMotions({99, 0}, "50k", lines), 49, 0, "50k");
  expectPos(simulateMotions({0, 0}, "20l", a1_long_line), 0, 20, "20l");
}

TEST_F(MiscMotionsTest, Count_ClampsAtBoundary) {
  expectPos(simulateMotions({0, 0}, "100j", a2_block_lines), 3, 0, "100j clamps to last line");
  expectPos(simulateMotions({3, 0}, "100k", a2_block_lines), 0, 0, "100k clamps to first line");
  expectPos(simulateMotions({0, 0}, "100l", a1_long_line), 0, 20, "100l clamps to line end");
  expectPos(simulateMotions({0, 10}, "100h", a1_long_line), 0, 0, "100h clamps to col 0");
}

TEST_F(MiscMotionsTest, Count_ggG) {
  auto lines = makeLines(100);
  expectPos(simulateMotions({50, 0}, "1gg", lines), 0, 0, "1gg goes to line 0");
  expectPos(simulateMotions({0, 0}, "10gg", lines), 9, 0, "10gg goes to line 9");
  expectPos(simulateMotions({0, 0}, "50gg", lines), 49, 0, "50gg goes to line 49");

  expectPos(simulateMotions({50, 0}, "1G", lines), 0, 0, "1G goes to line 0");
  expectPos(simulateMotions({0, 0}, "100G", lines), 99, 0, "100G goes to last line");
  expectPos(simulateMotions({0, 0}, "200G", lines), 99, 0, "200G clamps to last line");
}

TEST_F(MiscMotionsTest, Count_CharFind) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMotions({0, 0}, "2fa", lines), 0, 6, "2fa finds 2nd 'a' after cursor");
  expectPos(simulateMotions({0, 0}, "1fa", lines), 0, 3, "1fa finds 1st 'a' after cursor");
  expectPos(simulateMotions({0, 8}, "2Fa", lines), 0, 3, "2Fa finds 2nd 'a' backward");
  expectPos(simulateMotions({0, 0}, "ta", lines), 0, 2, "ta lands before 1st 'a'");
}

TEST_F(MiscMotionsTest, Count_CharFindWithRepeat) {
  Lines lines = {"abababab"};
  expectPos(simulateMotions({0, 0}, "2fa;", lines), 0, 6, "2fa; finds 2nd a then next");
  expectPos(simulateMotions({0, 0}, "fa;", lines), 0, 4, "fa; finds 1st a then next");
  expectPos(simulateMotions({0, 0}, "fa;;", lines), 0, 6, "fa;; finds 1st a then 2 more");
}

TEST_F(MiscMotionsTest, Count_CtrlD_SetsScrollAmount) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 0}, "5<C-d>", lines, nav), 5, 0, "5<C-d> moves 5 lines");
  expectPos(simulateWithNav({0, 0}, "10<C-d>", lines, nav), 10, 0, "10<C-d> moves 10 lines");
}

TEST_F(MiscMotionsTest, Count_CtrlU_SetsScrollAmount) {
  auto lines = makeLines(100);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({50, 0}, "5<C-u>", lines, nav), 45, 0, "5<C-u> moves up 5 lines");
  expectPos(simulateWithNav({50, 0}, "10<C-u>", lines, nav), 40, 0, "10<C-u> moves up 10 lines");
}

TEST_F(MiscMotionsTest, Count_CtrlF_RepeatsPages) {
  auto lines = makeLines(200);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({0, 0}, "2<C-f>", lines, nav), 76, 0, "2<C-f> moves 2 pages");
  expectPos(simulateWithNav({0, 0}, "3<C-f>", lines, nav), 114, 0, "3<C-f> moves 3 pages");
}

TEST_F(MiscMotionsTest, Count_CtrlB_RepeatsPages) {
  auto lines = makeLines(200);
  NavContext nav(40, 20);

  expectPos(simulateWithNav({100, 0}, "2<C-b>", lines, nav), 24, 0, "2<C-b> moves back 2 pages");
}

TEST_F(MiscMotionsTest, Count_MultiDigit) {
  auto lines = makeLines(150);
  expectPos(simulateMotions({0, 0}, "123j", lines), 123, 0, "123j moves down 123");
  expectPos(simulateMotions({149, 0}, "99k", lines), 50, 0, "99k moves up 99");
}

// =============================================================================
// 6. EDGE CASES
// =============================================================================

TEST_F(MiscMotionsTest, EmptyLineNavigation) {
  Lines lines = {""};
  expectPos(simulateMotions({0, 0}, "l", lines), 0, 0);
  expectPos(simulateMotions({0, 0}, "h", lines), 0, 0);
}

TEST_F(MiscMotionsTest, SingleCharLine) {
  Lines lines = {"a"};
  expectPos(simulateMotions({0, 0}, "l", lines), 0, 0);
  expectPos(simulateMotions({0, 0}, "h", lines), 0, 0);
}

TEST_F(MiscMotionsTest, FirstAndLastPositions) {
  EXPECT_EQ(simulateMotions({0, 0}, "gg", a2_block_lines).line, 0);

  int lastLine = a2_block_lines.size() - 1;
  Position p = simulateMotions({lastLine, 0}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
}

// =============================================================================
// 7. PROPERTY-BASED TESTS
// =============================================================================

TEST_F(MiscMotionsTest, Property_H_NeverIncreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = simulateMotions({0, col}, "h", a1_long_line);
    EXPECT_LE(result.col, col) << "h from col " << col << " should not increase col";
    EXPECT_EQ(result.line, 0) << "h should not change line";
  }
}

TEST_F(MiscMotionsTest, Property_L_NeverDecreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = simulateMotions({0, col}, "l", a1_long_line);
    EXPECT_GE(result.col, col) << "l from col " << col << " should not decrease col";
    EXPECT_EQ(result.line, 0) << "l should not change line";
  }
}

TEST_F(MiscMotionsTest, Property_J_NeverDecreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = simulateMotions({line, 0}, "j", a2_block_lines);
    EXPECT_GE(result.line, line) << "j from line " << line << " should not decrease line";
  }
}

TEST_F(MiscMotionsTest, Property_K_NeverIncreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = simulateMotions({line, 0}, "k", a2_block_lines);
    EXPECT_LE(result.line, line) << "k from line " << line << " should not increase line";
  }
}

TEST_F(MiscMotionsTest, Property_GG_AlwaysLine0) {
  auto lines = makeLines(20);
  for(int line = 0; line < (int)lines.size(); line++) {
    Position result = simulateMotions({line, 0}, "gg", lines);
    EXPECT_EQ(result.line, 0) << "gg from line " << line << " should go to line 0";
  }
}

TEST_F(MiscMotionsTest, Property_G_AlwaysLastLine) {
  auto lines = makeLines(20);
  int lastLine = lines.size() - 1;
  for(int line = 0; line < (int)lines.size(); line++) {
    Position result = simulateMotions({line, 0}, "G", lines);
    EXPECT_EQ(result.line, lastLine) << "G from line " << line << " should go to last line";
  }
}
