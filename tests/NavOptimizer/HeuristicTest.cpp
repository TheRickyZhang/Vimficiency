// tests/NavOptimizer/HeuristicTest.cpp
//
// Focused tests for NavOptimizer heuristic helpers.

#include <gtest/gtest.h>

#include "Optimizer/NavOptimizer/NavHeuristic.h"

TEST(NavOptimizerHeuristicTest, TreatsEOLTargetColAsCurrentColumn) {
  CharInterval range(CursorPos(0, 2), CursorPos(0, 2));
  CursorPos pos(0, 5, TARGETCOL_EOL);

  EXPECT_DOUBLE_EQ(NavHeuristic::distanceToRange(range, pos), 3.0);
}

TEST(NavOptimizerHeuristicTest, PreservesStickyTargetColumnAcrossLineDistance) {
  CharInterval range(CursorPos(3, 9), CursorPos(3, 9));
  CursorPos pos(0, 1, 5);

  EXPECT_DOUBLE_EQ(NavHeuristic::distanceToRange(range, pos), 7.0);
}

TEST(NavOptimizerHeuristicTest, UsesCurrentColumnWhenClosestGoalIsOnSameLine) {
  CharInterval range(CursorPos(0, 9), CursorPos(0, 9));
  CursorPos pos(0, 1, 5);

  EXPECT_DOUBLE_EQ(NavHeuristic::distanceToRange(range, pos), 8.0);
}
