#pragma once

#include "Types/Pos.h"

// Character-wise inclusive interval: [first, last].
struct CharInterval {
  Pos first;
  Pos last;

  constexpr CharInterval(Pos firstPos, Pos lastPos) : first(firstPos), last(lastPos) {}
  constexpr explicit CharInterval(Pos pos) : first(pos), last(pos) {}

  bool containsPos(const Pos& p) const {
    return first <= p && p <= last;
  }
};
