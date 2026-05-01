#pragma once

#include <sstream>
#include <string>
#include <ostream>

#include "Types/Sequence.h"

struct Result {
  Result() = default;
  Result(Sequence seq, double c) : sequence_(std::move(seq)), keyCost_(c) {}
  Result(const std::string& s, double c) : sequence_(s), keyCost_(c) {}
  Result(std::string&& s, double c) : sequence_(std::move(s)), keyCost_(c) {}

  const Sequence& getSequence() const { return sequence_; }
  double getCost() const { return keyCost_; }

  std::string to_string() {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }

  friend std::ostream& operator<<(std::ostream& os, const Result& r) {
    os << r.sequence_ << " " << r.keyCost_;
    return os;
  }

private:
  Sequence sequence_;
  double keyCost_ = 0;
};
