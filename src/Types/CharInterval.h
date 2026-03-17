#pragma once

#include "Types/CursorPos.h"

// Character-wise inclusive interval: [first, last].
// Intended for motion targets where both endpoints are real cursor landings.
struct CharInterval {
  CursorPos first;
  CursorPos last;

  CharInterval() = default;
  constexpr CharInterval(CursorPos firstPos, CursorPos lastPos) : first(firstPos), last(lastPos) {}

  bool isValid() const {
    return first.isValid() && last.isValid() && first <= last;
  }

  bool spansMultiple() const {
    return first.line != last.line;
  }

  bool containsPos(const CursorPos& p) const {
    return first <= p && p <= last;
  }
};

constexpr CharInterval CHAR_INTERVAL_OUTSIDE_BOUNDARY{
    POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};
