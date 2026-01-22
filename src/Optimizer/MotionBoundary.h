#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"

// MotionBoundary constrains the search space for MovementOptimizer.
//
// Like EditBoundary, stores information about what's outside the region:
// - hasLinesAbove/Below: for gg/G exclusion
// - leftColOffset: prefix length on first line (0 = no prefix)
// - rightColOffset: suffix length on last line (0 = no suffix)

struct MotionBoundary {
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;

  // Column offsets as lengths (like EditBoundary prefix/suffix)
  // 0 = no constraint
  int leftColOffset = 0;   // prefix length: positions < this on line 0 are forbidden
  int rightColOffset = 0;  // suffix length: positions >= (lineLen - this) on last line are forbidden

  // Default: no constraints
  MotionBoundary() = default;

  // Construct from buffer context
  MotionBoundary(const Lines& lines, Position firstPos, Position lastPos)
      : hasLinesAbove(firstPos.line > 0),
        hasLinesBelow(lastPos.line < static_cast<int>(lines.size()) - 1),
        leftColOffset(firstPos.col),
        rightColOffset(lines.empty() ? 0 :
            static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
    // Clamp rightColOffset to non-negative (lastPos at or past end of line = no suffix)
    if (rightColOffset < 0) rightColOffset = 0;
  }

  // Construct inheriting from parent boundary
  MotionBoundary(const MotionBoundary& parent, const Lines& lines,
                 Position firstPos, Position lastPos)
      : hasLinesAbove(parent.hasLinesAbove || firstPos.line > 0),
        hasLinesBelow(parent.hasLinesBelow || lastPos.line < static_cast<int>(lines.size()) - 1),
        leftColOffset(firstPos.col),
        rightColOffset(lines.empty() ? 0 :
            static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
    if (rightColOffset < 0) rightColOffset = 0;
  }

  // Helpers for gg/G exclusion (used by MovementOptimizer)
  bool excludeGG() const { return hasLinesAbove; }
  bool excludeG() const { return hasLinesBelow; }

  // Check if position is within bounds
  bool isPositionInBounds(const Position& pos, int lastLine, int lastLineLength) const {
    // First line: positions in prefix are forbidden
    if (leftColOffset > 0 && pos.line == 0 && pos.col < leftColOffset) {
      return false;
    }
    // Last line: positions in suffix are forbidden
    if (rightColOffset > 0 && pos.line == lastLine &&
        pos.col >= lastLineLength - rightColOffset) {
      return false;
    }
    return true;
  }
};
