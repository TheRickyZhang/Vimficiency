#pragma once

#include "Position.h"

// A character-wise region in the buffer. Always inclusive, can span across lines.
struct Range {
  Position first;
  Position last;

  Range() = default;
  constexpr Range(Position f, Position l) : first(f), last(l) {}

  // Ensure first <= last
  void normalize() {
    if (first > last) {
      first.swap(last);
    }
  }

  bool isValid() const {
    return first.isValid() && last.isValid();
  }

  bool spansMultiple() const {
    return first.line != last.line;
  }

  int size() const {
    return last.line - first.line + 1;
  }
};

// Sentinel value for "range outside boundary" / "operation would cross boundary"
constexpr Range RANGE_OUTSIDE_BOUNDARY{POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};
