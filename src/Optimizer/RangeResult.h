#pragma once

#include <string>
#include <ostream>

#include "Result.h"
#include "Types/CursorPos.h"

// Result with end position, used by optimizeToRange
struct RangeResult : Result {
  RangeResult() = default;
  RangeResult(Sequence seq, double c, CursorPos p)
    : Result(std::move(seq), c), goalPos_(p) {}
  RangeResult(const std::string& s, double c, CursorPos p)
    : Result(s, c), goalPos_(p) {}
  RangeResult(std::string&& s, double c, CursorPos p)
    : Result(std::move(s), c), goalPos_(p) {}

  const CursorPos& getGoalPos() const { return goalPos_; }
  void addGoalLineOffset(int offset) { goalPos_.line += offset; }

  friend std::ostream& operator<<(std::ostream& os, const RangeResult& r) {
    os << r.getSequence() << " " << r.getCost() << " -> (" << r.goalPos_.line << "," << r.goalPos_.col << ")";
    return os;
  }

private:
  CursorPos goalPos_{0, 0};
};
