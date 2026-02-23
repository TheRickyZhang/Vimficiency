// tests/Misc/MotionClassMaskTest.cpp
//
// Tests for MotionClassMask bitmask logic used in directional pruning.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="*MotionClassMask*"

#include <gtest/gtest.h>
#include "Optimizer/MotionOptimizer/MotionClassMask.h"
#include "VimTypes/Position.h"
#include <vector>
#include <utility>

using M = MotionClassMask;

// =============================================================================
// classesForSingleGoal tests - verify bitmask matches boolean logic
// =============================================================================

// Helper to compute expected classes using original boolean logic
static M expectedClassesForSingleGoal(int posLine, int posCol, int goalLine, int goalCol) {
  if (posLine == goalLine && posCol == goalCol) {
    return M::None;
  }

  bool wantLeft = false, wantRight = false;
  bool wantUp = false, wantDown = false;
  bool wantForward = false, wantBackward = false;

  if (posLine == goalLine) {
    if (posCol > goalCol) {
      wantLeft = wantBackward = true;
    } else {
      wantRight = wantForward = true;
    }
  } else if (posCol == goalCol) {
    if (posLine < goalLine) {
      wantDown = wantForward = true;
    } else {
      wantUp = wantBackward = true;
    }
  } else {
    if (posLine < goalLine) {
      wantDown = wantForward = true;
    } else {
      wantUp = wantBackward = true;
    }
    if (posCol > goalCol) {
      wantLeft = wantBackward = true;
    } else {
      wantRight = wantForward = true;
    }
  }

  M result = M::None;
  if (wantLeft) result = result | M::Left;
  if (wantRight) result = result | M::Right;
  if (wantUp) result = result | M::Up;
  if (wantDown) result = result | M::Down;
  if (wantForward) result = result | M::ForwardCross;
  if (wantBackward) result = result | M::BackwardCross;
  return result;
}

TEST(MotionClassMaskTest, AtGoalReturnsNone) {
  EXPECT_EQ(classesForSingleGoal(5, 5, 5, 5), M::None);
  EXPECT_EQ(classesForSingleGoal(0, 0, 0, 0), M::None);
}

TEST(MotionClassMaskTest, SameLineLeftOfGoal) {
  // Same line, col < goalCol => Right + ForwardCross
  M actual = classesForSingleGoal(5, 3, 5, 8);
  EXPECT_TRUE(has(actual, M::Right));
  EXPECT_TRUE(has(actual, M::ForwardCross));
  EXPECT_FALSE(has(actual, M::Left));
  EXPECT_FALSE(has(actual, M::BackwardCross));
  EXPECT_FALSE(has(actual, M::Up));
  EXPECT_FALSE(has(actual, M::Down));
}

TEST(MotionClassMaskTest, SameLineRightOfGoal) {
  // Same line, col > goalCol => Left + BackwardCross
  M actual = classesForSingleGoal(5, 8, 5, 3);
  EXPECT_TRUE(has(actual, M::Left));
  EXPECT_TRUE(has(actual, M::BackwardCross));
  EXPECT_FALSE(has(actual, M::Right));
  EXPECT_FALSE(has(actual, M::ForwardCross));
  EXPECT_FALSE(has(actual, M::Up));
  EXPECT_FALSE(has(actual, M::Down));
}

TEST(MotionClassMaskTest, SameColumnAboveGoal) {
  // Same col, line < goalLine => Down + ForwardCross
  M actual = classesForSingleGoal(3, 5, 8, 5);
  EXPECT_TRUE(has(actual, M::Down));
  EXPECT_TRUE(has(actual, M::ForwardCross));
  EXPECT_FALSE(has(actual, M::Up));
  EXPECT_FALSE(has(actual, M::BackwardCross));
  EXPECT_FALSE(has(actual, M::Left));
  EXPECT_FALSE(has(actual, M::Right));
}

TEST(MotionClassMaskTest, SameColumnBelowGoal) {
  // Same col, line > goalLine => Up + BackwardCross
  M actual = classesForSingleGoal(8, 5, 3, 5);
  EXPECT_TRUE(has(actual, M::Up));
  EXPECT_TRUE(has(actual, M::BackwardCross));
  EXPECT_FALSE(has(actual, M::Down));
  EXPECT_FALSE(has(actual, M::ForwardCross));
  EXPECT_FALSE(has(actual, M::Left));
  EXPECT_FALSE(has(actual, M::Right));
}

TEST(MotionClassMaskTest, MatchesExistingBooleanLogic) {
  // Test all 9 regions: same line (left/at/right), above (left/at/right), below (left/at/right)
  std::vector<std::pair<Position, Position>> testCases = {
      // Same line
      {{5, 10, 10}, {5, 5, 5}},   // left of goal
      {{5, 5, 5}, {5, 5, 5}},     // at goal
      {{5, 0, 0}, {5, 5, 5}},     // right of goal
      // Above
      {{3, 10, 10}, {5, 5, 5}},   // above-left
      {{3, 5, 5}, {5, 5, 5}},     // above
      {{3, 0, 0}, {5, 5, 5}},     // above-right
      // Below
      {{7, 10, 10}, {5, 5, 5}},   // below-left
      {{7, 5, 5}, {5, 5, 5}},     // below
      {{7, 0, 0}, {5, 5, 5}},     // below-right
  };

  for (const auto& [pos, goal] : testCases) {
    M expected = expectedClassesForSingleGoal(pos.line, pos.col, goal.line, goal.col);
    M actual = classesForSingleGoal(pos.line, pos.col, goal.line, goal.col);
    EXPECT_EQ(actual, expected)
        << "pos=(" << pos.line << "," << pos.col << ") "
        << "goal=(" << goal.line << "," << goal.col << ")";
  }
}

// =============================================================================
// classesForRangeWithTail tests
// =============================================================================

TEST(MotionClassMaskTest, InRangeReturnsNone) {
  // Inside range [rangeBegin, rangeEnd) should return None.
  EXPECT_EQ(classesForRangeWithTail(5, 5, 5, 3, 5, 11, 5, 10), M::None);  // Same line, between
  EXPECT_EQ(classesForRangeWithTail(6, 5, 5, 3, 8, 11, 8, 10), M::None);  // Middle line
  EXPECT_EQ(classesForRangeWithTail(5, 3, 5, 3, 8, 11, 8, 10), M::None);  // At rangeBegin
  EXPECT_EQ(classesForRangeWithTail(8, 10, 5, 3, 8, 11, 8, 10), M::None); // At range tail (end - 1)
}

TEST(MotionClassMaskTest, BeforeRangeOnSameLine) {
  // Same line as rangeBegin, but col < rangeBegin.col
  // Should include Right + ForwardCross (to reach rangeBegin)
  // And since range spans lines, Down is also valid
  M actual = classesForRangeWithTail(5, 0, 5, 5, 8, 11, 8, 10);
  EXPECT_TRUE(has(actual, M::Right));
  EXPECT_TRUE(has(actual, M::ForwardCross));
}

TEST(MotionClassMaskTest, AfterRangeOnSameLine) {
  // Same line as rangeEnd, but col >= rangeEnd.col
  // Should include Left + BackwardCross (to reach range tail)
  // And since range spans lines, Up is also valid
  M actual = classesForRangeWithTail(8, 15, 5, 3, 8, 11, 8, 10);
  EXPECT_TRUE(has(actual, M::Left));
  EXPECT_TRUE(has(actual, M::BackwardCross));
}

TEST(MotionClassMaskTest, AboveRange) {
  // Above range => Down + Forward
  M actual = classesForRangeWithTail(2, 5, 5, 3, 8, 11, 8, 10);
  EXPECT_TRUE(has(actual, M::Down));
  EXPECT_TRUE(has(actual, M::ForwardCross));
  EXPECT_FALSE(has(actual, M::Up));
}

TEST(MotionClassMaskTest, BelowRange) {
  // Below range => Up + Backward
  M actual = classesForRangeWithTail(12, 5, 5, 3, 8, 11, 8, 10);
  EXPECT_TRUE(has(actual, M::Up));
  EXPECT_TRUE(has(actual, M::BackwardCross));
  EXPECT_FALSE(has(actual, M::Down));
}

TEST(MotionClassMaskTest, RangeClassesContainEndpointUnion) {
  // Verify classesForRangeWithTail returns at least the union of classes for
  // rangeBegin and rangeTail.
  Position rangeBegin{5, 3, 3};
  Position rangeEnd{8, 11, 11};
  Position rangeTail{8, 10, 10};  // predecessor of rangeEnd

  std::vector<Position> testPositions = {
      {2, 0, 0}, {2, 5, 5}, {2, 15, 15},     // Above range
      {5, 0, 0},                              // Before on first line
      {8, 15, 15},                            // After on last line
      {10, 0, 0}, {10, 5, 5}, {10, 15, 15},  // Below range
  };

  for (const auto& pos : testPositions) {
    M endpointUnion = classesForSingleGoal(pos.line, pos.col, rangeBegin.line, rangeBegin.col)
                    | classesForSingleGoal(pos.line, pos.col, rangeTail.line, rangeTail.col);
    M actual = classesForRangeWithTail(pos.line, pos.col,
                                       rangeBegin.line, rangeBegin.col,
                                       rangeEnd.line, rangeEnd.col,
                                       rangeTail.line, rangeTail.col);

    // actual should contain at least the endpoint union (may have additional classes)
    EXPECT_EQ(actual & endpointUnion, endpointUnion)
        << "pos=(" << pos.line << "," << pos.col << ") "
        << "endpointUnion=" << static_cast<int>(endpointUnion) << " "
        << "actual=" << static_cast<int>(actual);
  }
}

TEST(MotionClassMaskTest, EndAtLineStartUsesPreviousLineTail) {
  // [5:3, 8:0) has tail on line 7 (not line 8 col -1).
  M actual = classesForRangeWithTail(9, 0, 5, 3, 8, 0, 7, 0);
  EXPECT_TRUE(has(actual, M::Up));
  EXPECT_TRUE(has(actual, M::BackwardCross));
}

TEST(MotionClassMaskTest, BoundaryLineEdgeCases) {
  // On first line of range, before first col, multi-line range => Down added
  M actual1 = classesForRangeWithTail(5, 0, 5, 5, 8, 11, 8, 10);
  EXPECT_TRUE(has(actual1, M::Down)) << "Should add Down when on first line but before range start";

  // On last line of range, after range end col, multi-line range => Up added
  M actual2 = classesForRangeWithTail(8, 15, 5, 5, 8, 11, 8, 10);
  EXPECT_TRUE(has(actual2, M::Up)) << "Should add Up when on last line but after range end";

  // Single-line range - no extra vertical needed
  M actual3 = classesForRangeWithTail(5, 0, 5, 5, 5, 11, 5, 10);
  // This is before on same line, so only needs Right + ForwardCross
  EXPECT_TRUE(has(actual3, M::Right));
  EXPECT_TRUE(has(actual3, M::ForwardCross));
}

// =============================================================================
// Bitmask operator tests
// =============================================================================

TEST(MotionClassMaskTest, BitwiseOr) {
  M a = M::Left | M::Up;
  EXPECT_TRUE(has(a, M::Left));
  EXPECT_TRUE(has(a, M::Up));
  EXPECT_FALSE(has(a, M::Right));
  EXPECT_FALSE(has(a, M::Down));
}

TEST(MotionClassMaskTest, BitwiseAnd) {
  M a = M::Left | M::Up | M::Right;
  M b = M::Left | M::Down;
  M c = a & b;
  EXPECT_TRUE(has(c, M::Left));
  EXPECT_FALSE(has(c, M::Up));
  EXPECT_FALSE(has(c, M::Right));
  EXPECT_FALSE(has(c, M::Down));
}

TEST(MotionClassMaskTest, AllMaskContainsAll) {
  EXPECT_TRUE(has(M::All, M::Left));
  EXPECT_TRUE(has(M::All, M::Right));
  EXPECT_TRUE(has(M::All, M::Up));
  EXPECT_TRUE(has(M::All, M::Down));
  EXPECT_TRUE(has(M::All, M::ForwardCross));
  EXPECT_TRUE(has(M::All, M::BackwardCross));
}
