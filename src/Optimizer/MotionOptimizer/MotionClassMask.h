#pragma once

#include <cstdint>

// Bitmask for 6 motion classes - allows O(1) union/intersection.
// Used by directional pruning to select which motion categories to explore
// based on relative position to goal/range.
enum class MotionClassMask : uint8_t {
  None          = 0,
  Left          = 1 << 0,  // h, 0, ^
  Right         = 1 << 1,  // l, $, ^
  Up            = 1 << 2,  // k, <C-u>, gg
  Down          = 1 << 3,  // j, <C-d>, G
  ForwardCross  = 1 << 4,  // w, W, e, E, }, )
  BackwardCross = 1 << 5,  // b, B, ge, gE, {, (
  All           = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5)
};

constexpr MotionClassMask operator|(MotionClassMask a, MotionClassMask b) {
  return static_cast<MotionClassMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr MotionClassMask operator&(MotionClassMask a, MotionClassMask b) {
  return static_cast<MotionClassMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool has(MotionClassMask mask, MotionClassMask c) {
  return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(c)) != 0;
}

// Compute motion classes needed to reach a single goal from a position.
// Returns bitmask of classes that should be explored.
constexpr MotionClassMask classesForSingleGoal(int posLine, int posCol,
                                                int goalLine, int goalCol) {
  using M = MotionClassMask;

  if (posLine == goalLine && posCol == goalCol) {
    return M::None;  // At goal
  }

  if (posLine == goalLine) {
    // Same line: only horizontal + crossing in goal direction
    return (posCol > goalCol)
        ? (M::Left | M::BackwardCross)
        : (M::Right | M::ForwardCross);
  }

  if (posCol == goalCol) {
    // Same column: only vertical + crossing in goal direction
    return (posLine < goalLine)
        ? (M::Down | M::ForwardCross)
        : (M::Up | M::BackwardCross);
  }

  // Different line and column: need vertical + horizontal + crossing
  MotionClassMask m = (posLine < goalLine)
      ? (M::Down | M::ForwardCross)
      : (M::Up | M::BackwardCross);

  m = m | ((posCol > goalCol)
      ? (M::Left | M::BackwardCross)
      : (M::Right | M::ForwardCross));

  return m;
}

// Compute motion classes needed to reach any point in a range [first, end).
// Returns union of classes for targeting rangeFirst vs last inclusive position,
// plus additional vertical classes for boundary line edge cases.
constexpr MotionClassMask classesForRange(int posLine, int posCol,
                                           int rfLine, int rfCol,
                                           int reLine, int reCol) {
  using M = MotionClassMask;

  // Check if in range [first, end) using lexicographic ordering
  bool afterFirst = (posLine > rfLine) || (posLine == rfLine && posCol >= rfCol);
  bool beforeEnd = (posLine < reLine) || (posLine == reLine && posCol < reCol);

  if (afterFirst && beforeEnd) {
    return M::None;  // Already in range
  }

  // Last inclusive position for goal targeting
  int rlLine = reLine;
  int rlCol = reCol - 1;

  // Take union of classes for targeting rangeFirst vs last inclusive position
  MotionClassMask m = classesForSingleGoal(posLine, posCol, rfLine, rfCol)
                    | classesForSingleGoal(posLine, posCol, rlLine, rlCol);

  // Additional: if on boundary line, may need extra vertical to enter range
  if (posLine == rfLine && posCol < rfCol && rfLine < reLine) {
    m = m | M::Down;  // Can go down into range
  }
  if (posLine == reLine && posCol >= reCol && rfLine < reLine) {
    m = m | M::Up;    // Can go up into range
  }

  return m;
}
