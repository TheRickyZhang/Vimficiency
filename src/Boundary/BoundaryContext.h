#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"

// =============================================================================
// BoundaryContext: Shared boundary offset/line info for constrained operations
// =============================================================================
// Stores the computed boundary information that both MotionBoundary and
// EditBoundary need:
// - hasLinesAbove/Below: for vertical boundary detection (gg/G, k/j escaping)
// - leftColOffset/rightColOffset: protected column ranges at line boundaries
//
// This struct extracts the common logic from both boundary types to ensure
// consistent boundary computation and enable easy conversion between them.
// =============================================================================

struct BoundaryContext {
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  int leftColOffset = 0;   // prefix length: columns < this on line 0 are forbidden
  int rightColOffset = 0;  // suffix length: columns >= (lineLen - this) on last line are forbidden

  BoundaryContext() = default;

  // Compute from buffer positions, optionally inheriting from parent context
  BoundaryContext(const Lines& lines, Position firstPos, Position lastPos,
                  const BoundaryContext& parent);

  // Compute from explicit flags (for FFI or when parent flags are known)
  BoundaryContext(const Lines& lines, Position firstPos, Position lastPos,
                  bool parentHasLinesAbove, bool parentHasLinesBelow);
};
