#pragma once

#include "Boundary/BoundaryContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

// Forward declaration
struct TransformBoundary;

// NavBoundary constrains the search space for NavOptimizer.
//
// Like TransformBoundary, stores information about what's outside the region:
// - hasLinesAbove/Below: for gg/G exclusion and edge-line checks
// - leftColOffset: prefix length on begin line (0 = no prefix)
// - rightColOffset: suffix length on end line (0 = no suffix)
//
// Uses BoundaryContext internally for shared offset/line logic.

class NavBoundary {
  BoundaryContext ctx_;

public:
  NavBoundary() = default;
  static const NavBoundary& noParent();

  // Construct from buffer context, optionally inheriting from parent
  // endPos is exclusive: one past the last valid cursor position on the end line
  NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                 const NavBoundary& parent = noParent());

  // The default constructor has NO PARENT, so basically any motions are possible
  // To specify restricted motions for lines, call with hasLinesBelow = false, hasLinesAbove = false

  // Construct with explicit external context flags (for FFI)
  // endPos is exclusive: one past the last valid cursor position on the end line
  NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                 bool hasLinesAbove, bool hasLinesBelow);

  // No TransformBoundary→NavBoundary conversion. The two carry the same
  // line-level flags, but TransformBoundary's leftColOffset/rightColOffset
  // describe the prefix/suffix being protected during a transform, which is
  // wrong context for visual-replay nav: the col clip would let motions
  // like `$` appear to land at the slice end while really landing inside
  // the protected suffix on replay. Construct NavBoundary explicitly with
  // only the flags you actually want.

  // Accessors delegate to ctx_
  bool hasLinesAbove() const { return ctx_.hasLinesAbove; }
  bool hasLinesBelow() const { return ctx_.hasLinesBelow; }
  int leftColOffset() const { return ctx_.leftColOffset; }
  int rightColOffset() const { return ctx_.rightColOffset; }

  // Access underlying context (for TransformBoundary construction)
  const BoundaryContext& context() const { return ctx_; }

  bool isPositionInBounds(const CursorPos& pos, int lastLine, int lastLineLength) const;
};
