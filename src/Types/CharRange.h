#pragma once

#include "Types/CursorPos.h"

// Character-wise half-open: [begin, end).
struct CharRange {
  CursorPos begin;
  CursorPos end;

  CharRange() = default;
  constexpr CharRange(CursorPos beginPos, CursorPos endPos) : begin(beginPos), end(endPos) {}

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

  bool containsPos(const CursorPos& p) const {
    return begin <= p && p < end;
  }

  int size() const {
    if (!isValid() || isEmpty()) return 0;
    return end.line - begin.line + 1;
  }
};

constexpr CharRange CHAR_RANGE_OUTSIDE_BOUNDARY{
    POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};
