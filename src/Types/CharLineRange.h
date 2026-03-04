#pragma once

#include "Types/CursorPos.h"

// A character-wise region that ends at the boundary before endLine.
// Half-open: [begin, line(endLine)).
struct CharLineRange {
  CursorPos begin;
  int endLine;

  constexpr CharLineRange(CursorPos beginPos, int endLine)
      : begin(beginPos), endLine(endLine) {}

  bool isValid() const {
    return begin.isValid() && endLine > begin.line;
  }

  int lineCountTouched() const {
    if (!isValid()) return 0;
    return endLine - begin.line;
  }
};

constexpr CharLineRange CHAR_LINE_RANGE_OUTSIDE_BOUNDARY(
    POSITION_OUTSIDE_BOUNDARY, -1);
