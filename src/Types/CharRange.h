#pragma once

#include <cassert>

#include "Types/CursorPos.h"

// A character-wise region in the buffer with a concrete exclusive end.
// Half-open: [begin, end).
// At public API boundaries, mixed char/line-boundary shapes should be modeled
// with CharLineRange / LineCharRange instead of smuggling that meaning through
// a special end column.
struct CharRange {
  CursorPos begin;
  CursorPos end;

  CharRange() = default;
  constexpr CharRange(CursorPos beginPos, CursorPos endPos)
      : begin(beginPos), end(endPos) {
    assert((!begin.isValid() && !end.isValid())
        || (begin.isValid() && end.isValid() && begin <= end));
  }

  bool isValid() const {
    assert(begin.isValid() == end.isValid());
    assert(!begin.isValid() || begin <= end);
    return begin.isValid();
  }

  bool spansMultiple() const {
    return begin.line != end.line;
  }

  bool isEmpty() const {
    assert(isValid());
    return begin.line == end.line && begin.col == end.col;
  }

  int size() const {
    assert(isValid());
    if (isEmpty()) return 0;
    return end.line - begin.line + 1;
  }
};

[[nodiscard]] inline CharRange orderedCharRange(const CursorPos& a, const CursorPos& b) {
  return (a <= b) ? CharRange(a, b) : CharRange(b, a);
}

[[nodiscard]] inline CharRange orderedCharRange(const CharRange& range) {
  return orderedCharRange(range.begin, range.end);
}

constexpr CharRange CHAR_RANGE_OUTSIDE_BOUNDARY{
    POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};
