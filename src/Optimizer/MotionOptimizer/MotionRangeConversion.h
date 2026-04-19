#pragma once

#include <cassert>
#include <optional>

#include "Types/CharInterval.h"
#include "Types/CharRange.h"
#include "Types/Lines.h"

// Canonical half-open [begin, end) -> inclusive [first, last] conversion
// for motion targets.
//
// Boundary rule:
// - Edit/diff code may use CharRange (half-open).
// - Motion code uses CharInterval (inclusive) only.
// - This helper is the only intended bridge between the two semantics.
inline std::optional<CharInterval> tryToMotionInterval(
    const Lines& lines, const CharRange& range) {
  assert(range.isValid());
  if (range.isEmpty()) return std::nullopt;
  CursorPos inclusiveLast = lines.getPrevPos(range.end);
  if (inclusiveLast < range.begin) return std::nullopt;
  return CharInterval(range.begin, inclusiveLast);
}

inline CharInterval toMotionInterval(const Lines& lines, const CharRange& range) {
  std::optional<CharInterval> interval = tryToMotionInterval(lines, range);
  assert(interval.has_value());
  return *interval;
}

inline CharInterval wholeLineMotionInterval(const Lines& lines, int line) {
  assert(line >= 0 && line < static_cast<int>(lines.size()));
  return CharInterval(CursorPos(line, 0), CursorPos(line, lines[line].lastCol()));
}
