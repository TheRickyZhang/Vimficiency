#include "BoundaryContext.h"

// Compute from buffer positions, inheriting from parent context
BoundaryContext::BoundaryContext(const Lines& lines, Position firstPos, Position lastPos,
                                 const BoundaryContext& parent)
    : hasLinesAbove(parent.hasLinesAbove || firstPos.line > 0),
      hasLinesBelow(parent.hasLinesBelow ||
                    lastPos.line + 1 < static_cast<int>(lines.size())),
      leftColOffset(firstPos.col),
      rightColOffset(lines.empty() ? 0 :
          static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}

// Compute from explicit flags (for FFI)
BoundaryContext::BoundaryContext(const Lines& lines, Position firstPos, Position lastPos,
                                 bool parentHasLinesAbove, bool parentHasLinesBelow)
    : hasLinesAbove(parentHasLinesAbove || firstPos.line > 0),
      hasLinesBelow(parentHasLinesBelow ||
                    lastPos.line + 1 < static_cast<int>(lines.size())),
      leftColOffset(firstPos.col),
      rightColOffset(lines.empty() ? 0 :
          static_cast<int>(lines[lastPos.line].size()) - 1 - lastPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}
