#pragma once

#include "Types/Pos.h"

class Lines;
struct CharRange;

// Character-wise inclusive interval: [first, last].
struct CharInterval {
  Pos first;
  Pos last;

  constexpr CharInterval(Pos firstPos, Pos lastPos) : first(firstPos), last(lastPos) {}
  constexpr explicit CharInterval(Pos pos) : first(pos), last(pos) {}

  // Convert a half-open [begin, end) CharRange to its inclusive [first, last]
  // motion form. The canonical bridge between edit/diff code (CharRange) and
  // motion code (CharInterval).
  //
  // Snap rule for past-end-of-line begin: when `begin` sits past the last
  // valid normal-mode cursor on its line (e.g. a `\n`-spanning diff with
  // begin = (L, lineEnd_L)), `getPrevPos(end)` lands at the cell just before
  // begin — the logical motion target since `begin` itself isn't a real
  // cursor position. Snap to a singleton interval at that predecessor.
  // The CharRange must be non-empty.
  CharInterval(const CharRange& range, const Lines& lines);

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
