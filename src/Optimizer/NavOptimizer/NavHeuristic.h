#pragma once

#include <cstdlib>

#include "Types/CharInterval.h"
#include "Types/CursorPos.h"

namespace NavHeuristic {

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
  return static_cast<double>(
      std::abs(closest.line - pos.line) +
      std::abs(closest.col - heuristicColToClosest(pos, closest)));
}

} // namespace NavHeuristic
