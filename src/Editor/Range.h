#pragma once

#include "Position.h"

// Range represents a character-wise region in the buffer.
// All ranges are inclusive (both start and end positions are included).
// Used by motions (d$, cw) and text objects (ciw, dap).
struct Range {
  Position start;
  Position end;

  Range() = default;
  constexpr Range(Position s, Position e) : start(s), end(e) {}

  // Ensure start <= end
  void normalize() {
    if (start > end) {
      start.swap(end);
    }
  }

  bool isValid() const {
    return start.isValid() && end.isValid();
  }
};

// Sentinel value for "no range" / "not found"
constexpr Range RANGE_NOT_FOUND{POSITION_NOT_FOUND, POSITION_NOT_FOUND};

// LineRange represents a line-wise region (for dd, dip, etc.)
// All line ranges are inclusive (both startLine and endLine are included).
struct LineRange {
  int startLine;
  int endLine;

  LineRange() : startLine(-1), endLine(-1) {}
  constexpr LineRange(int s, int e) : startLine(s), endLine(e) {}

  void normalize() {
    if (endLine < startLine) {
      std::swap(startLine, endLine);
    }
  }

  bool isValid() const {
    return startLine >= 0;
  }
};

// Sentinel value for "no line range" / "not found"
constexpr LineRange LINE_RANGE_NOT_FOUND{-1, -1};
