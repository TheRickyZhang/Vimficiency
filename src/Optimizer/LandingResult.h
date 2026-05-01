#pragma once

#include <string>
#include <ostream>

#include "Result.h"
#include "Types/CursorPos.h"

// A Result that also carries the cursor position where the motion landed.
// Returned by every NavOptimizer search (single-cursor and interval alike)
// since they all share one impl. For interval goals the landing varies
// across results; for the single-cursor convenience overload every result's
// landing is the input `goalPos`.
struct LandingResult : Result {
  LandingResult() = default;
  LandingResult(Sequence seq, double c, CursorPos p)
    : Result(std::move(seq), c), goalPos_(p) {}
  LandingResult(const std::string& s, double c, CursorPos p)
    : Result(s, c), goalPos_(p) {}
  LandingResult(std::string&& s, double c, CursorPos p)
    : Result(std::move(s), c), goalPos_(p) {}

  const CursorPos& getGoalPos() const { return goalPos_; }

  friend std::ostream& operator<<(std::ostream& os, const LandingResult& r) {
    os << r.getSequence() << " " << r.getCost() << " -> (" << r.goalPos_.line << "," << r.goalPos_.col << ")";
    return os;
  }

private:
  CursorPos goalPos_{0, 0};
};
