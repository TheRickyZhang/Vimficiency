#include "Commands/MiscMotionsTestHelpers.h"

using namespace std;

namespace {

TEST_F(MiscMotionsTest, Count_BasicHJKL) {
  expectPos(simulateMovements({0, 10}, "3h", a1_long_line), 0, 7, "3h moves left 3");
  expectPos(simulateMovements({0, 0}, "5l", a1_long_line), 0, 5, "5l moves right 5");
  expectPos(simulateMovements({0, 0}, "2j", a2_block_lines), 2, 0, "2j moves down 2");
  expectPos(simulateMovements({3, 0}, "2k", a2_block_lines), 1, 0, "2k moves up 2");
}

TEST_F(MiscMotionsTest, Count_LargeCount) {
  auto lines = makeLines(100);
  expectPos(simulateMovements({0, 0}, "50j", lines), 50, 0, "50j");
  expectPos(simulateMovements({99, 0}, "50k", lines), 49, 0, "50k");
  expectPos(simulateMovements({0, 0}, "20l", a1_long_line), 0, 20, "20l");
}

TEST_F(MiscMotionsTest, Count_ClampsAtBoundary) {
  expectPos(simulateMovements({0, 0}, "100j", a2_block_lines), 3, 0, "100j clamps to last line");
  expectPos(simulateMovements({3, 0}, "100k", a2_block_lines), 0, 0, "100k clamps to first line");
  expectPos(simulateMovements({0, 0}, "100l", a1_long_line), 0, 20, "100l clamps to line end");
  expectPos(simulateMovements({0, 10}, "100h", a1_long_line), 0, 0, "100h clamps to col 0");
}

TEST_F(MiscMotionsTest, Count_ggG) {
  auto lines = makeLines(100);
  expectPos(simulateMovements({50, 0}, "1gg", lines), 0, 0, "1gg goes to line 0");
  expectPos(simulateMovements({0, 0}, "10gg", lines), 9, 0, "10gg goes to line 9");
  expectPos(simulateMovements({0, 0}, "50gg", lines), 49, 0, "50gg goes to line 49");

  expectPos(simulateMovements({50, 0}, "1G", lines), 0, 0, "1G goes to line 0");
  expectPos(simulateMovements({0, 0}, "100G", lines), 99, 0, "100G goes to last line");
  expectPos(simulateMovements({0, 0}, "200G", lines), 99, 0, "200G clamps to last line");
}

TEST_F(MiscMotionsTest, Count_CharFind) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "2fa", lines), 0, 6, "2fa finds 2nd 'a' after cursor");
  expectPos(simulateMovements({0, 0}, "1fa", lines), 0, 3, "1fa finds 1st 'a' after cursor");
  expectPos(simulateMovements({0, 8}, "2Fa", lines), 0, 3, "2Fa finds 2nd 'a' backward");
  expectPos(simulateMovements({0, 0}, "ta", lines), 0, 2, "ta lands before 1st 'a'");
}

TEST_F(MiscMotionsTest, Count_CharFindWithRepeat) {
  Lines lines = {"abababab"};
  expectPos(simulateMovements({0, 0}, "2fa;", lines), 0, 6, "2fa; finds 2nd a then next");
  expectPos(simulateMovements({0, 0}, "fa;", lines), 0, 4, "fa; finds 1st a then next");
  expectPos(simulateMovements({0, 0}, "fa;;", lines), 0, 6, "fa;; finds 1st a then 2 more");
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
  expectPos(simulateMovements({0, 0}, "123j", lines), 123, 0, "123j moves down 123");
  expectPos(simulateMovements({149, 0}, "99k", lines), 50, 0, "99k moves up 99");
}

// =============================================================================
// 6. EDGE CASES
// =============================================================================

}  // namespace
