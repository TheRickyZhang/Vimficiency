#include "BoundaryContext.h"

// Compute from buffer positions, inheriting from parent context
BoundaryContext::BoundaryContext(const Lines& lines, Position firstPos, Position endPos,
                                 const BoundaryContext& parent)
    : hasLinesAbove(parent.hasLinesAbove || firstPos.line > 0),
      hasLinesBelow(parent.hasLinesBelow ||
                    endPos.line + 1 < static_cast<int>(lines.size())),
      leftColOffset(firstPos.col),
      rightColOffset(lines.empty() ? 0 :
          static_cast<int>(lines[endPos.line].size()) - endPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}

// Compute from explicit flags (for FFI)
BoundaryContext::BoundaryContext(const Lines& lines, Position firstPos, Position endPos,
                                 bool parentHasLinesAbove, bool parentHasLinesBelow)
    : hasLinesAbove(parentHasLinesAbove || firstPos.line > 0),
      hasLinesBelow(parentHasLinesBelow ||
                    endPos.line + 1 < static_cast<int>(lines.size())),
      leftColOffset(firstPos.col),
      rightColOffset(lines.empty() ? 0 :
          static_cast<int>(lines[endPos.line].size()) - endPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}
