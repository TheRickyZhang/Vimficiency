#pragma once

#include <cassert>

#include "Types/CursorPos.h"

// An inclusive range of valid character positions: [firstPos, lastPos].
// This is for motion targets, not half-open text spans.
struct InclusiveCharRange {
  Pos firstPos;
  Pos lastPos;

  constexpr InclusiveCharRange() = default;

  constexpr InclusiveCharRange(Pos first, Pos last)
      : firstPos(first), lastPos(last) {
    assert(firstPos.isValid() && lastPos.isValid() && firstPos <= lastPos);
  }

  constexpr InclusiveCharRange(CursorPos first, CursorPos last)
      : InclusiveCharRange(Pos(first.line, first.col), Pos(last.line, last.col)) {}

  bool contains(const Pos& pos) const {
    return pos >= firstPos && pos <= lastPos;
  }

  bool contains(const CursorPos& pos) const {
    return contains(Pos(pos.line, pos.col));
  }

  bool lineIntersects(int line) const {
    return line >= firstPos.line && line <= lastPos.line;
  }
};
