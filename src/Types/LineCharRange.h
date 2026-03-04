#pragma once

#include <cassert>

#include "Types/CursorPos.h"

// A character-wise region that begins at the boundary before beginLine.
// Half-open: [line(beginLine), end).
struct LineCharRange {
  int beginLine;
  CursorPos end;

  constexpr LineCharRange(int beginLine, CursorPos endPos)
      : beginLine(beginLine), end(endPos) {
    assert((beginLine == -1 && !end.isValid())
        || (beginLine >= 0 && end.isValid() && end.line >= beginLine));
  }

  bool isValid() const {
    assert((beginLine == -1 && !end.isValid())
        || (beginLine >= 0 && end.isValid() && end.line >= beginLine));
    return beginLine >= 0;
  }

  int lineCountTouched() const {
    assert(isValid());
    return end.line == beginLine ? 1 : (end.line - beginLine + 1);
  }
};

constexpr LineCharRange LINE_CHAR_RANGE_OUTSIDE_BOUNDARY(
    -1, POSITION_OUTSIDE_BOUNDARY);
