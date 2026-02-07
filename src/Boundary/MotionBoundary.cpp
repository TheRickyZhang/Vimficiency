#include "MotionBoundary.h"
#include "EditBoundary.h"

const MotionBoundary& MotionBoundary::noParent() {
  static const MotionBoundary instance{};
  return instance;
}

// Construct from buffer context, optionally inheriting from parent
MotionBoundary::MotionBoundary(const Lines& lines, Position firstPos, Position endPos,
                               const MotionBoundary& parent)
    : ctx_(lines, firstPos, endPos, parent.ctx_) {}

// Construct with explicit external context flags (for FFI)
MotionBoundary::MotionBoundary(const Lines& lines, Position firstPos, Position endPos,
                               bool hasLinesAbove, bool hasLinesBelow)
    : ctx_(lines, firstPos, endPos, hasLinesAbove, hasLinesBelow) {}

// Construct from EditBoundary
MotionBoundary::MotionBoundary(const EditBoundary& eb)
    : ctx_(eb.context()) {}

bool MotionBoundary::isPositionInBounds(const Position& pos, int lastLine, int lastLineLength) const {
  if (ctx_.leftColOffset > 0 && pos.line == 0 && pos.col < ctx_.leftColOffset) {
    return false;
  }
  if (ctx_.rightColOffset > 0 && pos.line == lastLine &&
      pos.col >= lastLineLength - ctx_.rightColOffset) {
    return false;
  }
  return true;
}
