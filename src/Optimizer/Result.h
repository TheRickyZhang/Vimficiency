#pragma once

#include <string>


struct Result {
  std::string sequence;
  double keyCost;

  Result(std::string s, double c) : sequence(s), keyCost(c) {}

  bool isValid() {
    return sequence.empty();
  }
};

