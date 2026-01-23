#include "MotionBoundary.h"

const MotionBoundary& MotionBoundary::noParent() {
  static const MotionBoundary instance{};
  return instance;
}

// Construct from buffer context, optionally inheriting from parent
MotionBoundary::MotionBoundary(const Lines& lines, Position firstPos, Position lastPos,
                               const MotionBoundary& parent)
    : hasLinesAbove_(parent.hasLinesAbove_ || firstPos.line > 0),
      hasLinesBelow_(parent.hasLinesBelow_ || lastPos.line < static_cast<int>(lines.size()) - 1),
      leftColOffset_(firstPos.col),
      rightColOffset_(lines.empty() ? 0 :
          static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
  if (rightColOffset_ < 0) rightColOffset_ = 0;
}

// Construct with explicit external context flags (for FFI)
MotionBoundary::MotionBoundary(const Lines& lines, Position firstPos, Position lastPos,
                               bool hasLinesAbove, bool hasLinesBelow)
    : hasLinesAbove_(hasLinesAbove || firstPos.line > 0),
      hasLinesBelow_(hasLinesBelow || lastPos.line < static_cast<int>(lines.size()) - 1),
      leftColOffset_(firstPos.col),
      rightColOffset_(lines.empty() ? 0 :
          static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
  if (rightColOffset_ < 0) rightColOffset_ = 0;
}

bool MotionBoundary::isPositionInBounds(const Position& pos, int lastLine, int lastLineLength) const {
  if (leftColOffset_ > 0 && pos.line == 0 && pos.col < leftColOffset_) {
    return false;
  }
  if (rightColOffset_ > 0 && pos.line == lastLine &&
      pos.col >= lastLineLength - rightColOffset_) {
    return false;
  }
  return true;
}
