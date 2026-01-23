#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"

// MotionBoundary constrains the search space for MotionOptimizer.
//
// Like EditBoundary, stores information about what's outside the region:
// - hasLinesAbove/Below: for gg/G exclusion and edge-line checks
// - leftColOffset: prefix length on first line (0 = no prefix)
// - rightColOffset: suffix length on last line (0 = no suffix)

class MotionBoundary {
  bool hasLinesAbove_ = false;
  bool hasLinesBelow_ = false;

  // Column offsets as lengths (like EditBoundary prefix/suffix)
  // 0 = no constraint
  int leftColOffset_ = 0;   // prefix length: positions < this on line 0 are forbidden
  int rightColOffset_ = 0;  // suffix length: positions >= (lineLen - this) on last line are forbidden

public:
  MotionBoundary() = default;
  static const MotionBoundary& noParent();

  // Construct from buffer context, optionally inheriting from parent
  MotionBoundary(const Lines& lines, Position firstPos, Position lastPos,
                 const MotionBoundary& parent = noParent());

  // Construct with explicit external context flags (for FFI)
  MotionBoundary(const Lines& lines, Position firstPos, Position lastPos,
                 bool hasLinesAbove, bool hasLinesBelow);

  // Accessors
  bool hasLinesAbove() const { return hasLinesAbove_; }
  bool hasLinesBelow() const { return hasLinesBelow_; }
  int leftColOffset() const { return leftColOffset_; }
  int rightColOffset() const { return rightColOffset_; }

  bool isPositionInBounds(const Position& pos, int lastLine, int lastLineLength) const;
};
