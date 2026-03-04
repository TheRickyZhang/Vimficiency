#pragma once

#include <cassert>

#include "Types/CursorPos.h"

// A character-wise region that ends at the boundary before endLine.
// Half-open: [begin, line(endLine)).
struct CharLineRange {
  CursorPos begin;
  int endLine;

  constexpr CharLineRange(CursorPos beginPos, int endLine)
      : begin(beginPos), endLine(endLine) {
    assert((!begin.isValid() && endLine == -1)
        || (begin.isValid() && endLine > begin.line));
  }

  bool isValid() const {
    assert((!begin.isValid() && endLine == -1)
        || (begin.isValid() && endLine > begin.line));
    return begin.isValid();
  }

  int lineCountTouched() const {
    assert(isValid());
    return endLine - begin.line;
  }
};

constexpr CharLineRange CHAR_LINE_RANGE_OUTSIDE_BOUNDARY(
    POSITION_OUTSIDE_BOUNDARY, -1);
