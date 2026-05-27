#pragma once

#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>

namespace PropertyDomains {

struct DiffCaseSpec {
  std::vector<std::string> initial;
  std::vector<std::string> goal;
  bool identity;
};

inline auto LineTextDomain(int minLen, int maxLen) {
  return fuzztest::PrintableAsciiString()
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

inline auto LineVecDomain(
    int minLines, int maxLines, int minLineLen, int maxLineLen) {
  return fuzztest::VectorOf(LineTextDomain(minLineLen, maxLineLen))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

inline auto DiffCaseSpecDomain() {
  return fuzztest::StructOf<DiffCaseSpec>(
      LineVecDomain(1, 6, 0, 40),
      LineVecDomain(1, 6, 0, 40),
      fuzztest::Arbitrary<bool>());
}

}  // namespace PropertyDomains
