#pragma once

#include "Boundary/BoundaryContext.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

// Forward declaration
struct EditBoundary;

// MotionBoundary constrains the search space for MotionOptimizer.
//
// Like EditBoundary, stores information about what's outside the region:
// - hasLinesAbove/Below: for gg/G exclusion and edge-line checks
// - leftColOffset: prefix length on first line (0 = no prefix)
// - rightColOffset: suffix length on last line (0 = no suffix)
//
// Uses BoundaryContext internally for shared offset/line logic.

class MotionBoundary {
  BoundaryContext ctx_;

public:
  MotionBoundary() = default;
  static const MotionBoundary& noParent();

  // Construct from buffer context, optionally inheriting from parent
  MotionBoundary(const Lines& lines, Position firstPos, Position lastPos,
                 const MotionBoundary& parent = noParent());

  // Construct with explicit external context flags (for FFI)
  MotionBoundary(const Lines& lines, Position firstPos, Position lastPos,
                 bool hasLinesAbove, bool hasLinesBelow);

  // Construct from EditBoundary (for conversion when switching optimizer types)
  explicit MotionBoundary(const EditBoundary& eb);

  // Accessors delegate to ctx_
  bool hasLinesAbove() const { return ctx_.hasLinesAbove; }
  bool hasLinesBelow() const { return ctx_.hasLinesBelow; }
  int leftColOffset() const { return ctx_.leftColOffset; }
  int rightColOffset() const { return ctx_.rightColOffset; }

  // Access underlying context (for EditBoundary construction)
  const BoundaryContext& context() const { return ctx_; }

  bool isPositionInBounds(const Position& pos, int lastLine, int lastLineLength) const;
};
