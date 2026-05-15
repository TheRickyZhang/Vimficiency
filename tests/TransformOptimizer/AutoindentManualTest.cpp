#include "TransformOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_MatchingIndent) {
  // cj with matching indent: autoindent matches goal, no adjustment needed
  Lines initial = {"    aaa", "    bbb"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_ExcessAutoindent) {
  // Source line has more indent than goal: autoindent 8, goal 4
  Lines initial = {"        aaa", "    bbb"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_InsufficientAutoindent) {
  // Source line has less indent than goal: autoindent 2, goal 8
  Lines initial = {"  aaa", "    bbb"};
  Lines goal = {"        xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_CountedCC) {
  // {n}cc with indent: counted linewise change on indented lines
  Lines initial = {"    aaa", "    bbb", "    ccc"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_WithBoundaryContext) {
  // Linewise change with surrounding lines (hasLinesAbove/Below)
  Lines fullBuffer = {"context_above", "    aaa", "    bbb", "context_below"};
  CursorPos initialPos(1, 0), endPos(2, 7);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);
  Lines goal = {"    xxx"};

  TransformResult res = opt.optimizeTransform(editRegion, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  Lines expectedFull = {"context_above", "    xxx", "context_below"};
  forEachValidResult(res.getResults(), editRegion, [&](CursorPos pos, const auto& seq) {
    CursorPos fullPos(pos.line + initialPos.line, pos.col);
    SimulationResult nvim = oracle->simulate(fullBuffer, fullPos.line, fullPos.col, seq.str());
    EXPECT_EQ(nvim.lines, expectedFull)
        << "Goal mismatch for seq='" << seq << "' from " << fullPos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << fullPos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_NoIndent) {
  // Linewise change on unindented lines with indented goal
  Lines initial = {"aaa", "bbb"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_CollapseWithBS) {
  // Exercises the collapse path where cursorLine > 0 (BS in collapse).
  // ck from line 2 would change lines [1,2], beginLine=1, needing BS to join
  // with prefix line above. Autoindent from indented source line.
  Lines fullBuffer = {"prefix", "    aaa", "    bbb", "suffix"};
  CursorPos initialPos(1, 0), endPos(2, 7);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);
  Lines goal = {"    xxx"};

  TransformResult res = opt.optimizeTransform(editRegion, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  Lines expectedFull = {"prefix", "    xxx", "suffix"};
  forEachValidResult(res.getResults(), editRegion, [&](CursorPos pos, const auto& seq) {
    CursorPos fullPos(pos.line + initialPos.line, pos.col);
    SimulationResult nvim = oracle->simulate(fullBuffer, fullPos.line, fullPos.col, seq.str());
    EXPECT_EQ(nvim.lines, expectedFull)
        << "Goal mismatch for seq='" << seq << "' from " << fullPos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << fullPos;
  });
}

}  // namespace
