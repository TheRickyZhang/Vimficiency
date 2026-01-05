#pragma once

#include <string>
#include <ostream>

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

