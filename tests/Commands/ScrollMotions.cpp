#include "Commands/MiscMotionsTestHelpers.h"

using namespace std;

namespace {

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

  CursorPos result = simulateWithNav({0, 10}, "<C-d>", lines, nav);
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

  CursorPos p1 = simulateWithNav({30, 0}, "<C-d>", lines, nav);
  CursorPos p2 = simulateWithNav(p1, "<C-u>", lines, nav);
  EXPECT_EQ(p2.line, 30) << "C-d then C-u should return to original";

  CursorPos p3 = simulateWithNav({30, 0}, "<C-f>", lines, nav);
  CursorPos p4 = simulateWithNav(p3, "<C-b>", lines, nav);
  EXPECT_EQ(p4.line, 30) << "C-f then C-b should return to original";
}

TEST_F(MiscMotionsTest, Scroll_MultipleScrolls) {
  auto lines = makeLines(100);
  NavContext nav(40, 10);

  CursorPos p = {0, 0};
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

  CursorPos p = simulateWithNav({25, 0}, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-d with scroll=0 should not move";

  p = simulateWithNav({25, 0}, "<C-u>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-u with scroll=0 should not move";
}

TEST_F(MiscMotionsTest, Scroll_WindowHeightTwo) {
  auto lines = makeLines(50);
  NavContext nav(2, 1);

  CursorPos p = simulateWithNav({25, 0}, "<C-f>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-f with window=2 should not move";

  p = simulateWithNav({25, 0}, "<C-b>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-b with window=2 should not move";
}

// =============================================================================
// 5. COUNT PREFIXES (for motions not in other files)
// =============================================================================

}  // namespace
