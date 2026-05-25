#pragma once

#include <string>
#include <utility>
#include <vector>

#include <fuzztest/fuzztest.h>

#include "Keyboard/Key.h"
#include "Keyboard/PhysicalKeys.h"
#include "Types/Lines.h"

namespace PropertyDomains {

struct DiffCaseSpec {
  std::vector<std::string> initial;
  std::vector<std::string> goal;
  bool identity;
};

inline auto KeyIdsDomain(int minLen, int maxLen) {
  return fuzztest::VectorOf(fuzztest::InRange<int>(0, KEY_COUNT - 1))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

inline PhysicalKeys toPhysicalKeys(const std::vector<int>& keyIds) {
  PhysicalKeys keys;
  keys.reserve(static_cast<int>(keyIds.size()));
  for (int keyId : keyIds) {
    keys.push_back(static_cast<Key>(keyId));
  }
  return keys;
}

inline auto LineTextDomain(int maxLen) {
  return fuzztest::PrintableAsciiString().WithMaxSize(maxLen);
}

inline auto LineVecDomain(int minLines, int maxLines, int maxLineLen) {
  return fuzztest::VectorOf(LineTextDomain(maxLineLen))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

inline Lines toLines(std::vector<std::string> lines) {
  if (lines.empty()) {
    lines.push_back("");
  }
  return Lines(std::move(lines));
}

inline auto DiffCaseSpecDomain() {
  return fuzztest::StructOf<DiffCaseSpec>(
      LineVecDomain(1, 6, 40),
      LineVecDomain(1, 6, 40),
      fuzztest::Arbitrary<bool>());
}

}  // namespace PropertyDomains
