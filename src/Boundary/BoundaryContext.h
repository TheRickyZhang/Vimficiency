#pragma once

#include "Types/CursorPos.h"
#include "Types/Lines.h"

// =============================================================================
// BoundaryContext: Shared boundary offset/line info for constrained operations
// =============================================================================
// Stores the computed boundary information that both NavBoundary and
// TransformBoundary need:
// - hasLinesAbove/Below: hidden outside-line context for escape-capable motions
// - leftColOffset/rightColOffset: protected column ranges at line boundaries
//
// This struct extracts the common logic from both boundary types to ensure
// consistent boundary computation and enable easy conversion between them.
// =============================================================================

struct BoundaryContext {
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  int leftColOffset = 0;   // prefix length: columns < this on line 0 are forbidden
  int rightColOffset = 0;  // suffix length: columns >= (lineLen - this) on end line are forbidden

  BoundaryContext() = default;

  // Compute from buffer positions, optionally inheriting from parent context
  // endPos is exclusive: one past the last valid cursor position on the end line
  BoundaryContext(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                  const BoundaryContext& parent);

  // Compute from explicit flags (for FFI or when parent flags are known)
  // endPos is exclusive: one past the last valid cursor position on the end line
  BoundaryContext(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                  bool parentHasLinesAbove, bool parentHasLinesBelow);
};
