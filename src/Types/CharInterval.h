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

  // Mirrors `Pos::isValid()` / `CharRange::isValid()`: both endpoints must be
  // valid positions and form a non-reversed inclusive interval. An inclusive
  // interval with `first == last` is a single-char interval, which is
  // non-empty and therefore valid.
  bool isValid() const {
    return first.isValid() && last.isValid() && first <= last;
  }
};
