#include "NavBoundary.h"
#include "TransformBoundary.h"

const NavBoundary& NavBoundary::noParent() {
  static const NavBoundary instance{};
  return instance;
}

// Construct from buffer context, optionally inheriting from parent
NavBoundary::NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                               const NavBoundary& parent)
    : ctx_(lines, beginPos, endPos, parent.ctx_) {}

// Construct with explicit external context flags (for FFI)
NavBoundary::NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                               bool hasLinesAbove, bool hasLinesBelow)
    : ctx_(lines, beginPos, endPos, hasLinesAbove, hasLinesBelow) {}

bool NavBoundary::isPositionInBounds(const CursorPos& pos, int lastLine, int lastLineLength) const {
  if (ctx_.leftColOffset > 0 && pos.line == 0 && pos.col < ctx_.leftColOffset) {
    return false;
  }
  if (ctx_.rightColOffset > 0 && pos.line == lastLine &&
      pos.col >= lastLineLength - ctx_.rightColOffset) {
    return false;
  }
  return true;
}
