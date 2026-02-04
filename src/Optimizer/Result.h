#pragma once

#include <sstream>
#include <string>
#include <ostream>

#include "State/Sequence.h"

struct Result {
  Sequence sequence;
  double keyCost;

  Result() : keyCost(0) {}
  Result(Sequence seq, double c) : sequence(std::move(seq)), keyCost(c) {}
  Result(const std::string& s, double c) : sequence(s), keyCost(c) {}
  Result(std::string&& s, double c) : sequence(std::move(s)), keyCost(c) {}

  bool isValid() const {
    return !sequence.empty();
  }

  // Get string representation
  std::string getSequenceString() const {
    return sequence.keys;
  }

  std::string to_string() {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }

  friend std::ostream& operator<<(std::ostream& os, const Result& r) {
    os << r.sequence << " " << r.keyCost;
    return os;
  }
};
