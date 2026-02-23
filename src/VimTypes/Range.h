#pragma once

#include "VimTypes/Lines.h"
#include "VimTypes/Position.h"

// A character-wise region in the buffer. Half-open: [begin, end).
struct Range {
  Position begin;
  Position end;

  Range() = default;
  constexpr Range(Position beginPos, Position endPos) : begin(beginPos), end(endPos) {}

  // Ensure begin <= end (lexicographic on line/col).
  void normalize() {
    if (begin > end) {
      begin.swap(end);
    }
  }

  bool isValid() const {
    return begin.isValid() && end.isValid() && end >= begin;
  }

  bool spansMultiple() const {
    return begin.line != end.line;
  }

  bool isEmpty() const {
    return begin.line == end.line && begin.col == end.col;
  }

  // Number of lines touched by the half-open range.
  // Examples:
  // - [L0:c0, L0:c1) => 1
  // - [L0:c0, L1:0)  => 1
  // - [L0:c0, L1:c1) => 2
  // - empty range    => 0
  int size() const {
    if (!isValid() || isEmpty()) return 0;
    if (begin.line == end.line) return 1;
    return end.col == 0 ? (end.line - begin.line)
                        : (end.line - begin.line + 1);
  }
};

// Sentinel value for "range outside boundary" / "operation would cross boundary"
constexpr Range RANGE_OUTSIDE_BOUNDARY{POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};
