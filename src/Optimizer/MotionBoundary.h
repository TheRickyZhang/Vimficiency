#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"

// MotionBoundary constrains the search space for MovementOptimizer.
//
// Like EditBoundary, stores information about what's outside the region:
// - hasLinesAbove/Below: for gg/G exclusion and edge-line checks
// - leftColOffset: prefix length on first line (0 = no prefix)
// - rightColOffset: suffix length on last line (0 = no suffix)

class MotionBoundary {
  bool hasLinesAbove_ = false;
  bool hasLinesBelow_ = false;

  // Column offsets as lengths (like EditBoundary prefix/suffix)
  // 0 = no constraint
  int leftColOffset_ = 0;   // prefix length: positions < this on line 0 are forbidden
  int rightColOffset_ = 0;  // suffix length: positions >= (lineLen - this) on last line are forbidden

public:
  // Default: no constraints
  MotionBoundary() = default;

  // Direct construction with explicit values
  MotionBoundary(bool hasLinesAbove, bool hasLinesBelow,
                 int leftColOffset = 0, int rightColOffset = 0)
      : hasLinesAbove_(hasLinesAbove),
        hasLinesBelow_(hasLinesBelow),
        leftColOffset_(leftColOffset),
        rightColOffset_(rightColOffset) {}

  // Construct from buffer context
  MotionBoundary(const Lines& lines, Position firstPos, Position lastPos)
      : hasLinesAbove_(firstPos.line > 0),
        hasLinesBelow_(lastPos.line < static_cast<int>(lines.size()) - 1),
        leftColOffset_(firstPos.col),
        rightColOffset_(lines.empty() ? 0 :
            static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
    // Clamp rightColOffset to non-negative (lastPos at or past end of line = no suffix)
    if (rightColOffset_ < 0) rightColOffset_ = 0;
  }

  // Construct inheriting from parent boundary
  MotionBoundary(const MotionBoundary& parent, const Lines& lines,
                 Position firstPos, Position lastPos)
      : hasLinesAbove_(parent.hasLinesAbove_ || firstPos.line > 0),
        hasLinesBelow_(parent.hasLinesBelow_ || lastPos.line < static_cast<int>(lines.size()) - 1),
        leftColOffset_(firstPos.col),
        rightColOffset_(lines.empty() ? 0 :
            static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
    if (rightColOffset_ < 0) rightColOffset_ = 0;
  }

  // Accessors
  bool hasLinesAbove() const { return hasLinesAbove_; }
  bool hasLinesBelow() const { return hasLinesBelow_; }
  int leftColOffset() const { return leftColOffset_; }
  int rightColOffset() const { return rightColOffset_; }

  // Check if position is within bounds
  bool isPositionInBounds(const Position& pos, int lastLine, int lastLineLength) const {
    // First line: positions in prefix are forbidden
    if (leftColOffset_ > 0 && pos.line == 0 && pos.col < leftColOffset_) {
      return false;
    }
    // Last line: positions in suffix are forbidden
    if (rightColOffset_ > 0 && pos.line == lastLine &&
        pos.col >= lastLineLength - rightColOffset_) {
      return false;
    }
    return true;
  }
};
