#pragma once

#include <cstdint>
#include "Types/Pos.h"

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
constexpr MotionClassMask classesForSingleGoal(Pos pos, Pos goal) {
  using M = MotionClassMask;

  if (pos == goal) {
    return M::None;
  }

  if (pos.line == goal.line) {
    return (pos.col > goal.col)
        ? (M::Left | M::BackwardCross)
        : (M::Right | M::ForwardCross);
  }

  if (pos.col == goal.col) {
    return (pos.line < goal.line)
        ? (M::Down | M::ForwardCross)
        : (M::Up | M::BackwardCross);
  }

  MotionClassMask m = (pos.line < goal.line)
      ? (M::Down | M::ForwardCross)
      : (M::Up | M::BackwardCross);

  m = m | ((pos.col > goal.col)
      ? (M::Left | M::BackwardCross)
      : (M::Right | M::ForwardCross));

  return m;
}

// Compute motion classes needed to reach any point in [rangeFirst, rangeLast] (inclusive).
// Takes Pos objects — only geometric position matters for directional pruning.
constexpr MotionClassMask classesForRange(Pos pos, Pos rangeFirst, Pos rangeLast) {
  using M = MotionClassMask;

  // Check if in range [rangeFirst, rangeLast] using lexicographic ordering.
  if (pos >= rangeFirst && pos <= rangeLast) {
    return M::None;  // Already in range
  }

  // Take union of classes for targeting rangeFirst vs rangeLast.
  MotionClassMask m = classesForSingleGoal(pos, rangeFirst)
                    | classesForSingleGoal(pos, rangeLast);

  // Additional: if on boundary line, may need extra vertical to enter range
  if (pos.line == rangeFirst.line && pos.col < rangeFirst.col && rangeFirst.line < rangeLast.line) {
    m = m | M::Down;  // Can go down into range
  }
  if (pos.line == rangeLast.line && pos.col > rangeLast.col && rangeFirst.line < rangeLast.line) {
    m = m | M::Up;    // Can go up into range
  }

  return m;
}
