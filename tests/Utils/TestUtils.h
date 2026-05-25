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

// Check if any result contains a sequence starting with prefix
template <typename ResultT>
inline bool hasSequenceStartingWith(const std::vector<ResultT>& results,
                                    const std::string& prefix) {
  return std::any_of(results.begin(), results.end(),
      [&prefix](const ResultT& r) {
        return r.getSequence().size() >= prefix.size() &&
               r.getSequence().view().substr(0, prefix.size()) == prefix;
      });
}

// Print results for manual inspection (limited to first N)
template <typename ResultT>
inline void printResultsDebug(const std::vector<ResultT>& results,
                              const std::string& description) {
  std::cerr << "\n=== " << description << " ===" << std::endl;
  for (size_t i = 0; i < results.size(); i++) {
    std::cerr << "  " << i << ": " << results[i].getSequence()
              << " (cost=" << results[i].getCost() << ")" << std::endl;
  }
}

// =============================================================================
// Key Adjustments for Config
// =============================================================================

struct KeyAdjustment {
  Key k;
  double cost;
  KeyAdjustment(Key k, double cost) : k(k), cost(cost) {}
};

// =============================================================================
// Test File Loading
// =============================================================================

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

// =============================================================================
// Legacy Functions (declared, defined in TestUtils.cpp)
// =============================================================================

Lines readLines(std::istream& in);

template <typename ResultT>
bool contains_all(const std::vector<ResultT>& v,
                  std::initializer_list<std::string> need) {
  std::unordered_set<std::string> s;
  for (const auto& r : v) s.insert(r.getSequence().str());
  for (const auto& x : need) if (s.find(x) == s.end()) return false;
  return true;
}

template <typename ResultT>
void printResults(std::vector<ResultT>& results) {
  std::cout << "Results (" << results.size() << ") : " << std::endl;
  for (const auto& r : results) {
    std::cout << r << " ";
  }
  std::cout << std::endl;
}
