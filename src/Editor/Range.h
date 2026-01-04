#pragma once

#include "Position.h"

// Range represents a region in the buffer for operator application.
// Used by both motion-based operations (d$, cw) and text objects (ciw, dap).
struct Range {
  Position start;
  Position end;
  bool linewise = false;   // If true, operation affects whole lines (dd, dip)
  bool inclusive = true;   // If true, end position is included (f vs t)

  Range(Position s, Position e, bool lw = false, bool incl = true)
    : start(s), end(e), linewise(lw), inclusive(incl) {}

  // Ensure start <= end (normalize)
  void normalize() {
    if (start.line > end.line || (start.line == end.line && start.col > end.col)) {
      std::swap(start, end);
    }
  }

  bool isEmpty() const {
    return start.line == end.line && start.col == end.col && !inclusive;
  }
};

// Convert a motion result to a Range
// motionInclusive: true for f/F/e/E motions, false for t/T/w/b motions
inline Range rangeFromMotion(Position from, Position to, bool motionInclusive = false) {
  Range r(from, to, false, motionInclusive);
  r.normalize();
  return r;
}
