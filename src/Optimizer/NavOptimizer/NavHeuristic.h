#pragma once

#include <array>
#include <cmath>
#include <cstdlib>

#include "Types/CharInterval.h"
#include "Types/CursorPos.h"

namespace NavHeuristic {

// Heuristic hot path: table the sqrt of small integer distances (libm call ->
// L1 load). Distances past the table (large buffers) fall back to std::sqrt.
constexpr int SQRT_TABLE_SIZE = 1024;

inline const std::array<double, SQRT_TABLE_SIZE> SQRT_TABLE = [] {
  std::array<double, SQRT_TABLE_SIZE> table{};
  for (int i = 0; i < SQRT_TABLE_SIZE; ++i)
    table[i] = std::sqrt(static_cast<double>(i));
  return table;
}();

inline double fastSqrt(int n) {
  return n < SQRT_TABLE_SIZE ? SQRT_TABLE[n] : std::sqrt(static_cast<double>(n));
}

inline int heuristicTargetCol(const CursorPos& pos) {
  return pos.targetCol == TARGETCOL_EOL ? pos.col : pos.targetCol;
}

inline int heuristicColToClosest(const CursorPos& pos, const Pos& closest) {
  if (closest.line == pos.line) return pos.col;
  return heuristicTargetCol(pos);
}

inline double distanceToRange(const CharInterval& range, const CursorPos& pos) {
  if (range.containsPos(pos)) return 0.0;

  Pos closest = (pos < range.first) ? range.first : range.last;
  int dline = std::abs(closest.line - pos.line);
  int dcol = std::abs(closest.col - heuristicColToClosest(pos, closest));
  return fastSqrt(dline) + fastSqrt(dcol);
}

} // namespace NavHeuristic
