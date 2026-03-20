// tests/MotionOptimizer/HeuristicTest.cpp
//
// Focused tests for MotionOptimizer heuristic helpers.

#include <gtest/gtest.h>

#include "Optimizer/MotionOptimizer/MotionHeuristic.h"

TEST(MotionOptimizerHeuristicTest, TreatsEOLTargetColAsCurrentColumn) {
  CharInterval range(CursorPos(0, 2), CursorPos(0, 2));
  CursorPos pos(0, 5, TARGETCOL_EOL);

  EXPECT_DOUBLE_EQ(MotionHeuristic::distanceToRange(range, pos), 3.0);
}

TEST(MotionOptimizerHeuristicTest, PreservesStickyTargetColumnAcrossLineDistance) {
  CharInterval range(CursorPos(3, 9), CursorPos(3, 9));
  CursorPos pos(0, 1, 5);

  EXPECT_DOUBLE_EQ(MotionHeuristic::distanceToRange(range, pos), 7.0);
}

TEST(MotionOptimizerHeuristicTest, UsesCurrentColumnWhenClosestGoalIsOnSameLine) {
  CharInterval range(CursorPos(0, 9), CursorPos(0, 9));
  CursorPos pos(0, 1, 5);

  EXPECT_DOUBLE_EQ(MotionHeuristic::distanceToRange(range, pos), 8.0);
}

// targetCol larger than goal col (sticky col from a longer line).
// Heuristic uses targetCol directly — may underestimate because Vim clamps,
// but underestimation is safe for A* admissibility.
TEST(MotionOptimizerHeuristicTest, StickyTargetColExceedsGoalCol) {
  // pos on line 0 col 2, sticky targetCol 40 (from a long line).
  // Goal at (2, 5). Cross-line: dy=2, dx=|5-40|=35 → 37.
  // Actual Vim distance is likely smaller (clamp to shorter line), so this
  // is an overestimate — still safe since A* just explores more.
  CharInterval range(CursorPos(2, 5), CursorPos(2, 5));
  CursorPos pos(0, 2, 40);

  EXPECT_DOUBLE_EQ(MotionHeuristic::distanceToRange(range, pos), 37.0);
}

// Default-constructed CursorPos has targetCol=0, col=0 — both zero, no sentinel.
TEST(MotionOptimizerHeuristicTest, DefaultConstructedCursorPos) {
  CharInterval range(CursorPos(1, 3), CursorPos(1, 3));
  CursorPos pos;  // (0, 0, targetCol=0)

  EXPECT_DOUBLE_EQ(MotionHeuristic::distanceToRange(range, pos), 4.0);
}
