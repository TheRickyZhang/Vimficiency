// tests/Unit/Misc/NavClassMaskTest.cpp
//
// Unit tests for NavClassMask directional pruning masks.

#include <gtest/gtest.h>
#include "Optimizer/NavOptimizer/NavClassMask.h"
#include <vector>

using M = NavClassMask;

// =============================================================================
// classesForSingleGoal tests
// =============================================================================

TEST(NavClassMaskTest, AtGoalReturnsNone) {
  EXPECT_EQ(classesForSingleGoal({5, 3}, {5, 3}), M::None);
}

TEST(NavClassMaskTest, GoalRight) {
  M m = classesForSingleGoal({5, 3}, {5, 10});
  EXPECT_TRUE(has(m, M::Right));
  EXPECT_TRUE(has(m, M::ForwardCross));
  EXPECT_FALSE(has(m, M::Left));
  EXPECT_FALSE(has(m, M::Up));
  EXPECT_FALSE(has(m, M::Down));
}

TEST(NavClassMaskTest, GoalLeft) {
  M m = classesForSingleGoal({5, 10}, {5, 3});
  EXPECT_TRUE(has(m, M::Left));
  EXPECT_TRUE(has(m, M::BackwardCross));
  EXPECT_FALSE(has(m, M::Right));
}

TEST(NavClassMaskTest, GoalBelow) {
  M m = classesForSingleGoal({3, 5}, {8, 5});
  EXPECT_TRUE(has(m, M::Down));
  EXPECT_TRUE(has(m, M::ForwardCross));
  EXPECT_FALSE(has(m, M::Up));
  EXPECT_FALSE(has(m, M::Left));
  EXPECT_FALSE(has(m, M::Right));
}

TEST(NavClassMaskTest, GoalAbove) {
  M m = classesForSingleGoal({8, 5}, {3, 5});
  EXPECT_TRUE(has(m, M::Up));
  EXPECT_TRUE(has(m, M::BackwardCross));
  EXPECT_FALSE(has(m, M::Down));
}

TEST(NavClassMaskTest, GoalBelowRight) {
  M m = classesForSingleGoal({3, 2}, {8, 10});
  EXPECT_TRUE(has(m, M::Down));
  EXPECT_TRUE(has(m, M::Right));
  EXPECT_TRUE(has(m, M::ForwardCross));
  EXPECT_FALSE(has(m, M::Up));
  EXPECT_FALSE(has(m, M::Left));
}

TEST(NavClassMaskTest, GoalAboveLeft) {
  M m = classesForSingleGoal({8, 10}, {3, 2});
  EXPECT_TRUE(has(m, M::Up));
  EXPECT_TRUE(has(m, M::Left));
  EXPECT_TRUE(has(m, M::BackwardCross));
  EXPECT_FALSE(has(m, M::Down));
  EXPECT_FALSE(has(m, M::Right));
}

TEST(NavClassMaskTest, GoalBelowLeft) {
  M m = classesForSingleGoal({3, 10}, {8, 2});
  EXPECT_TRUE(has(m, M::Down));
  EXPECT_TRUE(has(m, M::ForwardCross));
  EXPECT_TRUE(has(m, M::Left));
  EXPECT_TRUE(has(m, M::BackwardCross));
}

TEST(NavClassMaskTest, GoalAboveRight) {
  M m = classesForSingleGoal({8, 2}, {3, 10});
  EXPECT_TRUE(has(m, M::Up));
  EXPECT_TRUE(has(m, M::BackwardCross));
  EXPECT_TRUE(has(m, M::Right));
  EXPECT_TRUE(has(m, M::ForwardCross));
}

// =============================================================================
// Edge cases
// =============================================================================

TEST(NavClassMaskTest, SameLineSameCol) {
  EXPECT_EQ(classesForSingleGoal({0, 0}, {0, 0}), M::None);
}

TEST(NavClassMaskTest, AllBitsAreDistinct) {
  EXPECT_NE(M::Left, M::Right);
  EXPECT_NE(M::Up, M::Down);
  EXPECT_NE(M::ForwardCross, M::BackwardCross);
}

TEST(NavClassMaskTest, BitwiseOperations) {
  M m = M::Left | M::Up;
  EXPECT_TRUE(has(m, M::Left));
  EXPECT_TRUE(has(m, M::Up));
  EXPECT_FALSE(has(m, M::Right));
  EXPECT_FALSE(has(m, M::Down));
}

// =============================================================================
// classesForRange tests (inclusive first/last semantics)
// =============================================================================

TEST(NavClassMaskTest, InRangeReturnsNone) {
  // Inside range [first, last] should return None.
  // range: first=(5,3) last=(5,10) — single line
  EXPECT_EQ(classesForRange({5, 5}, {5, 3}, {5, 10}), M::None);  // Same line, between
  // range: first=(5,3) last=(8,10) — multi-line
  EXPECT_EQ(classesForRange({6, 5}, {5, 3}, {8, 10}), M::None);  // Middle line
  EXPECT_EQ(classesForRange({5, 3}, {5, 3}, {8, 10}), M::None);  // At rangeFirst
  EXPECT_EQ(classesForRange({8, 10}, {5, 3}, {8, 10}), M::None); // At rangeLast
}

TEST(NavClassMaskTest, BeforeRangeOnSameLine) {
  // Same line as rangeFirst, but col < rangeFirst.col
  // Should include Right + ForwardCross (to reach rangeFirst)
  // And since range spans lines, Down is also valid
  M actual = classesForRange({5, 0}, {5, 5}, {8, 10});
  EXPECT_TRUE(has(actual, M::Right));
  EXPECT_TRUE(has(actual, M::ForwardCross));
}

TEST(NavClassMaskTest, AfterRangeOnSameLine) {
  // Same line as rangeLast, but col > rangeLast.col
  // Should include Left + BackwardCross (to reach rangeLast)
  // And since range spans lines, Up is also valid
  M actual = classesForRange({8, 15}, {5, 3}, {8, 10});
  EXPECT_TRUE(has(actual, M::Left));
  EXPECT_TRUE(has(actual, M::BackwardCross));
}

TEST(NavClassMaskTest, AboveRange) {
  // Above range => Down + Forward
  M actual = classesForRange({2, 5}, {5, 3}, {8, 10});
  EXPECT_TRUE(has(actual, M::Down));
  EXPECT_TRUE(has(actual, M::ForwardCross));
  EXPECT_FALSE(has(actual, M::Up));
}

TEST(NavClassMaskTest, BelowRange) {
  // Below range => Up + Backward
  M actual = classesForRange({12, 5}, {5, 3}, {8, 10});
  EXPECT_TRUE(has(actual, M::Up));
  EXPECT_TRUE(has(actual, M::BackwardCross));
  EXPECT_FALSE(has(actual, M::Down));
}

TEST(NavClassMaskTest, RangeClassesContainEndpointUnion) {
  // Verify classesForRange returns at least the union of classes for
  // rangeFirst and rangeLast.
  Pos rangeFirst{5, 3};
  Pos rangeLast{8, 10};

  std::vector<Pos> testPositions = {
      {2, 0}, {2, 5}, {2, 15},     // Above range
      {5, 0},                       // Before on first line
      {8, 15},                      // After on last line
      {10, 0}, {10, 5}, {10, 15},  // Below range
  };

  for (const auto& pos : testPositions) {
    M endpointUnion = classesForSingleGoal(pos, rangeFirst)
                    | classesForSingleGoal(pos, rangeLast);
    M actual = classesForRange(pos, rangeFirst, rangeLast);

    // actual should contain at least the endpoint union (may have additional classes)
    EXPECT_EQ(actual & endpointUnion, endpointUnion)
        << "pos=(" << pos.line << "," << pos.col << ") "
        << "endpointUnion=" << static_cast<int>(endpointUnion) << " "
        << "actual=" << static_cast<int>(actual);
  }
}

TEST(NavClassMaskTest, LastAtLineStartUsesPreviousLine) {
  // range first=(5,3), last=(7,0) — last is at start of line 7
  M actual = classesForRange({9, 0}, {5, 3}, {7, 0});
  EXPECT_TRUE(has(actual, M::Up));
  EXPECT_TRUE(has(actual, M::BackwardCross));
}

TEST(NavClassMaskTest, BoundaryLineEdgeCases) {
  // On first line of range, before first col, multi-line range => Down added
  M actual1 = classesForRange({5, 0}, {5, 5}, {8, 10});
  EXPECT_TRUE(has(actual1, M::Down)) << "Should add Down when on first line but before range start";

  // On last line of range, after rangeLast col, multi-line range => Up added
  M actual2 = classesForRange({8, 15}, {5, 5}, {8, 10});
  EXPECT_TRUE(has(actual2, M::Up)) << "Should add Up when on last line but after range end";

  // Single-line range - no extra vertical needed
  M actual3 = classesForRange({5, 0}, {5, 5}, {5, 10});
  // This is before on same line, so only needs Right + ForwardCross
  EXPECT_TRUE(has(actual3, M::Right));
  EXPECT_TRUE(has(actual3, M::ForwardCross));
}
