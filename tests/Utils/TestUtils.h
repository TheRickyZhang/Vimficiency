#pragma once

#include "Keyboard/Key.h"
#include "Types/Lines.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

// =============================================================================
// Result Inspection Helpers
// =============================================================================

// Check if any result contains the exact sequence. Generic over any Result
// subclass so it works with both `Result` and `LandingResult` lists.
template <typename ResultT>
inline bool hasSequence(const std::vector<ResultT>& results, const std::string& seq) {
  return std::any_of(results.begin(), results.end(),
      [&seq](const ResultT& r) { return r.getSequence() == seq; });
}

struct KeyAdjustment {
  Key k;
  double cost;
  KeyAdjustment(Key k, double cost) : k(k), cost(cost) {}
};

namespace TestFiles {

inline Lines load(const std::string& filename) {
  auto path = std::filesystem::path(__FILE__).parent_path() / ".." / ".." /
              "data" / "TestFiles" / filename;
  std::ifstream file(path);
  if (!file) {
    assert(false && "Cannot open test file");
  }
  Lines lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace TestFiles

template <typename ResultT>
bool contains_all(const std::vector<ResultT>& v,
                  std::initializer_list<std::string> need) {
  std::unordered_set<std::string> s;
  for (const auto& r : v) s.insert(r.getSequence().str());
  for (const auto& x : need) if (s.find(x) == s.end()) return false;
  return true;
}
