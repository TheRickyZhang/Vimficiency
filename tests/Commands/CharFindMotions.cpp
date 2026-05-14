#include "Commands/MiscMotionsTestHelpers.h"

using namespace std;

namespace {

TEST_F(MiscMotionsTest, F_FindForward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 0}, "fc", lines), 0, 2, "f from 0 to 'c'");
  expectPos(simulateMovements({0, 0}, "fj", lines), 0, 9, "f from 0 to 'j'");
  expectPos(simulateMovements({0, 3}, "fg", lines), 0, 6, "f from 3 to 'g'");
}

TEST_F(MiscMotionsTest, F_FindForward_NotFound) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 0}, "fz", lines), 0, 0, "f to nonexistent char");
  expectPos(simulateMovements({0, 5}, "fa", lines), 0, 5, "f forward to char behind cursor");
}

TEST_F(MiscMotionsTest, F_FindBackward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 9}, "Fc", lines), 0, 2, "F from 9 to 'c'");
  expectPos(simulateMovements({0, 9}, "Fa", lines), 0, 0, "F from 9 to 'a'");
  expectPos(simulateMovements({0, 6}, "Fd", lines), 0, 3, "F from 6 to 'd'");
}

TEST_F(MiscMotionsTest, F_FindBackward_NotFound) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 9}, "Fz", lines), 0, 9, "F to nonexistent char");
  expectPos(simulateMovements({0, 3}, "Fj", lines), 0, 3, "F backward to char ahead of cursor");
}

TEST_F(MiscMotionsTest, T_TillForward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 0}, "tc", lines), 0, 1, "t from 0 to before 'c'");
  expectPos(simulateMovements({0, 0}, "tj", lines), 0, 8, "t from 0 to before 'j'");
  expectPos(simulateMovements({0, 3}, "tg", lines), 0, 5, "t from 3 to before 'g'");
}

TEST_F(MiscMotionsTest, T_TillForward_AdjacentChar) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 0}, "tb", lines), 0, 0, "t to adjacent char stays");
}

TEST_F(MiscMotionsTest, T_TillBackward_Basic) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 9}, "Tc", lines), 0, 3, "T from 9 to after 'c'");
  expectPos(simulateMovements({0, 9}, "Ta", lines), 0, 1, "T from 9 to after 'a'");
}

TEST_F(MiscMotionsTest, T_TillBackward_AdjacentChar) {
  Lines lines = {"abcdefghij"};
  expectPos(simulateMovements({0, 5}, "Te", lines), 0, 5, "T to adjacent char stays");
}

TEST_F(MiscMotionsTest, CharFind_WithSemicolonRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "fa", lines), 0, 3, "fa from 0");
  expectPos(simulateMovements({0, 0}, "fa;", lines), 0, 6, "fa; from 0");
  expectPos(simulateMovements({0, 8}, "Fa", lines), 0, 6, "Fa from 8");
  expectPos(simulateMovements({0, 8}, "Fa;", lines), 0, 3, "Fa; from 8");
  expectPos(simulateMovements({0, 8}, "Fa;;", lines), 0, 0, "Fa;; from 8");
}

TEST_F(MiscMotionsTest, CharFind_WithCommaRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "fa,", lines), 0, 0, "fa, from 0 - back to start");
  expectPos(simulateMovements({0, 0}, "fa;,", lines), 0, 3, "fa;, from 0 - forward twice, back once");
  expectPos(simulateMovements({0, 8}, "Fa,", lines), 0, 6, "Fa, from 8 - stays at 6");
}

TEST_F(MiscMotionsTest, CharFind_MixedRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "fa;,;", lines), 0, 6, "fa;,; complex repeat");
}

TEST_F(MiscMotionsTest, CharFind_TillWithRepeat) {
  Lines lines = {"abcabcabc"};
  expectPos(simulateMovements({0, 0}, "ta", lines), 0, 2, "ta from 0 - before first 'a' at 3");
  expectPos(simulateMovements({0, 0}, "ta;", lines), 0, 2, "ta; from 0 - stuck at 2");
  expectPos(simulateMovements({0, 0}, "fa;", lines), 0, 6, "fa; from 0 advances properly");
}

TEST_F(MiscMotionsTest, CharFind_SpaceAsTarget) {
  Lines lines = {"abc def ghi"};
  expectPos(simulateMovements({0, 0}, "f ", lines), 0, 3, "f<space> from 0");
  expectPos(simulateMovements({0, 0}, "f ;", lines), 0, 7, "f<space>; from 0");
  expectPos(simulateMovements({0, 0}, "t ", lines), 0, 2, "t<space> from 0");
  expectPos(simulateMovements({0, 10}, "F ", lines), 0, 7, "F<space> from end");
  expectPos(simulateMovements({0, 10}, "F ;", lines), 0, 3, "F<space>; from end");
}

TEST_F(MiscMotionsTest, CharFind_MultipleOccurrences) {
  Lines lines = {"aaaaaa"};
  expectPos(simulateMovements({0, 0}, "fa", lines), 0, 1, "fa in all-a line");
  expectPos(simulateMovements({0, 0}, "fa;", lines), 0, 2, "fa; in all-a line");
  expectPos(simulateMovements({0, 0}, "fa;;", lines), 0, 3, "fa;; in all-a line");
  expectPos(simulateMovements({0, 0}, "fa;;;", lines), 0, 4, "fa;;; in all-a line");
}

TEST_F(MiscMotionsTest, CharFind_CombinedWithOtherMotions) {
  Lines lines = {"abcdefghij", "0123456789"};
  expectPos(simulateMovements({0, 0}, "jfc", lines), 1, 0, "j then fc (no 'c' in line 1)");
  expectPos(simulateMovements({0, 0}, "fcj", lines), 1, 2, "fc then j");
}

TEST_F(MiscMotionsTest, CharFind_AtLineEnd) {
  Lines lines = {"abcdef"};
  expectPos(simulateMovements({0, 5}, "fa", lines), 0, 5, "f from end - target behind");
  expectPos(simulateMovements({0, 0}, "Ff", lines), 0, 0, "F from start - target ahead");
}

TEST_F(MiscMotionsTest, CharFind_OnTargetChar) {
  Lines lines = {"abcabc"};
  expectPos(simulateMovements({0, 0}, "fa", lines), 0, 3, "fa when on 'a' finds next 'a'");
  expectPos(simulateMovements({0, 3}, "Fa", lines), 0, 0, "Fa when on 'a' finds previous 'a'");
}

// =============================================================================
// 4. SCROLL MOTIONS (<C-d>, <C-u>, <C-f>, <C-b>)
// =============================================================================

static CursorPos simulateWithNav(CursorPos start, const string& motion,
                                const Lines& lines, NavContext nav) {
  return simulateMovements(start, motion, lines, nav);
}

static Lines makeLines(int count) {
  Lines lines;
  for (int i = 0; i < count; i++) {
    lines.push_back("line" + to_string(i));
  }
  return lines;
}

}  // namespace
