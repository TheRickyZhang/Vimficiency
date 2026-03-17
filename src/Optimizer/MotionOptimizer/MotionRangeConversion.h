#pragma once

#include <cassert>

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
inline CharInterval toMotionInterval(const Lines& lines, const CharRange& range) {
  assert(range.isValid());
  assert(!range.isEmpty());
  CursorPos inclusiveLast = lines.getPrevPos(range.end);
  assert(inclusiveLast >= range.begin);
  return CharInterval(range.begin, inclusiveLast);
}
