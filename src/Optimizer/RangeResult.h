#pragma once

#include <string>
#include <ostream>

#include "Types/CursorPos.h"
#include "Types/Sequence.h"

// Result with end position, used by optimizeToRange
struct RangeResult {
  Sequence sequence;
  double keyCost;
  CursorPos goalPos;

  RangeResult() : keyCost(0), goalPos(0, 0) {}
  RangeResult(Sequence seq, double c, CursorPos p)
    : sequence(std::move(seq)), keyCost(c), goalPos(p) {}
  RangeResult(const std::string& s, double c, CursorPos p)
    : sequence(s), keyCost(c), goalPos(p) {}
  RangeResult(std::string&& s, double c, CursorPos p)
    : sequence(std::move(s)), keyCost(c), goalPos(p) {}

  bool isValid() const {
    return !sequence.empty();
  }

  const Sequence& getSequenceString() const {
    return sequence;
  }

  friend std::ostream& operator<<(std::ostream& os, const RangeResult& r) {
    os << r.sequence << " " << r.keyCost << " -> (" << r.goalPos.line << "," << r.goalPos.col << ")";
    return os;
  }
};
