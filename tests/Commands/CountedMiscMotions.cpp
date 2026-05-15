#include "Commands/MiscMotionsTestHelpers.h"

using namespace std;

namespace {

// Counted basic motions (h/l/j/k, gg/G, f/F/t/T with ;/, repeat, and multi-digit
// counts) are covered against the oracle by
// tests/Properties/CountedMotionProperties.cpp. NavContext-dependent scroll
// motions (<C-d>, <C-u>, <C-f>, <C-b>) stay here because the oracle does not
// receive the optimizer's NavContext.

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

}  // namespace
