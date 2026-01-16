#pragma once

#include "Position.h"

// A character-wise region in the buffer. Always inclusive, can span across lines.
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

// Sentinel value for "range outside boundary" / "operation would cross boundary"
constexpr Range RANGE_OUTSIDE_BOUNDARY(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
