#pragma once

#include <string>
#include <ostream>

#include "Editor/Position.h"

struct Result {
  std::string sequence;
  double keyCost;

  Result(std::string s, double c) : sequence(s), keyCost(c) {}

  bool isValid() const {
    return !sequence.empty();
  }
};

inline std::ostream& operator<<(std::ostream& os, const Result& r) {
  os << r.sequence << ", " << r.keyCost << "\n";
  return os;
}

// Result with end position, used by optimizeToRange
struct RangeResult {
  std::string sequence;
  double keyCost;
  Position endPos;

  RangeResult(std::string s, double c, Position p)
    : sequence(std::move(s)), keyCost(c), endPos(p) {}

  bool isValid() const {
    return !sequence.empty();
  }
};

