#include "BoundaryContext.h"

// Compute from buffer positions, inheriting from parent context
BoundaryContext::BoundaryContext(const Lines& lines, Position beginPos, Position endPos,
                                 const BoundaryContext& parent)
    : hasLinesAbove(parent.hasLinesAbove || beginPos.line > 0),
      hasLinesBelow(parent.hasLinesBelow ||
                    endPos.line + 1 < static_cast<int>(lines.size())),
      leftColOffset(beginPos.col),
      rightColOffset(lines.empty() ? 0 :
          static_cast<int>(lines[endPos.line].size()) - endPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}

// Compute from explicit flags (for FFI)
BoundaryContext::BoundaryContext(const Lines& lines, Position beginPos, Position endPos,
                                 bool parentHasLinesAbove, bool parentHasLinesBelow)
    : hasLinesAbove(parentHasLinesAbove || beginPos.line > 0),
      hasLinesBelow(parentHasLinesBelow ||
                    endPos.line + 1 < static_cast<int>(lines.size())),
      leftColOffset(beginPos.col),
      rightColOffset(lines.empty() ? 0 :
          static_cast<int>(lines[endPos.line].size()) - endPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}
