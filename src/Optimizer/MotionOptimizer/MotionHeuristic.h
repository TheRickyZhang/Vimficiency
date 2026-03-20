#pragma once

#include <cstdlib>

#include "Types/CharInterval.h"
#include "Types/CursorPos.h"

namespace MotionHeuristic {

// Returns the effective horizontal position for heuristic scoring.
// targetCol is always valid (set via setCol or constructor; no "unset" sentinel).
// TARGETCOL_EOL (INT_MAX) is the only special value and falls back to pos.col.
// When targetCol > actual line length the heuristic may underestimate (Vim clamps),
// which is safe for A* admissibility.
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

}  // namespace MotionHeuristic
