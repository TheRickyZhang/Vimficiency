#pragma once

#include "Optimizer/Result.h"
#include "Keyboard/KeyboardModel.h"
#include "Utils/Lines.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

// =============================================================================
// Result Inspection Helpers
// =============================================================================

// Check if any result contains the exact sequence
inline bool hasSequence(const std::vector<Result>& results, const std::string& seq) {
  return std::any_of(results.begin(), results.end(),
      [&seq](const Result& r) { return r.sequence == seq; });
}

// Check if any result contains a sequence starting with prefix
inline bool hasSequenceStartingWith(const std::vector<Result>& results,
                                    const std::string& prefix) {
  return std::any_of(results.begin(), results.end(),
      [&prefix](const Result& r) {
        return r.sequence.size() >= prefix.size() &&
               r.sequence.view().substr(0, prefix.size()) == prefix;
      });
}

// Print results for manual inspection (limited to first N)
inline void printResultsDebug(const std::vector<Result>& results,
                              const std::string& description) {
  std::cerr << "\n=== " << description << " ===" << std::endl;
  for (size_t i = 0; i < results.size(); i++) {
    std::cerr << "  " << i << ": " << results[i].sequence
              << " (cost=" << results[i].keyCost << ")" << std::endl;
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
    throw std::runtime_error("Cannot open: " + path.string());
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

bool contains_all(const std::vector<Result>& v,
                  std::initializer_list<std::string> need);

void printResults(std::vector<Result>& results);

