#include "Levenshtein.h"

#include <algorithm>
#include <cmath>
#include <functional>

std::string join(const std::vector<std::string>& lines) {
  if (lines.empty()) return "";

  std::string result;
  size_t totalSize = 0;
  for (const auto& line : lines) {
    totalSize += line.size() + 1;  // +1 for newline
  }
  result.reserve(totalSize);

  for (size_t i = 0; i < lines.size(); ++i) {
    result += lines[i];
    if (i + 1 < lines.size()) {
      result += '\n';
    }
  }
  return result;
}


Levenshtein::Levenshtein(std::string goal, double deletionCost)
  : goal_(std::move(goal)), deletionCost_(deletionCost)
{
  // Initialize base row: distance from empty string to goal[0..j]
  // This is always j insertions (cost 1 each), regardless of deletionCost
  baseRow_.resize(goal_.size() + 1);
  for (size_t j = 0; j <= goal_.size(); ++j) {
    baseRow_[j] = static_cast<double>(j);
  }
}

int Levenshtein::distance(const std::vector<std::string>& lines) const {
    return distance(join(lines));
}

int Levenshtein::distance(const std::string& source) const {
  return static_cast<int>(std::round(distanceDouble(source)));
}

double Levenshtein::distanceDouble(const std::string& source) const {
  if (source == goal_) return 0.0;
  if (source.empty()) return static_cast<double>(goal_.size());
  // Deleting all of source costs deletionCost_ per char
  if (goal_.empty()) return deletionCost_ * static_cast<double>(source.size());

  // Find longest cached prefix
  size_t cachedPrefixLen = 0;
  const std::vector<double>* startRow = &baseRow_;

  // Try progressively shorter prefixes until we find a cache hit
  // For efficiency, we could use a trie, but hash lookup is simple and fast
  for (size_t len = source.size(); len > 0; --len) {
    size_t h = hashPrefix(source, len);
    auto it = prefixCache_.find(h);
    if (it != prefixCache_.end()) {
      cachedPrefixLen = len;
      startRow = &it->second;
      break;
    }
  }

  // Compute remaining rows
  std::vector<double> prevRow = *startRow;
  std::vector<double> currRow(goal_.size() + 1);

  for (size_t i = cachedPrefixLen; i < source.size(); ++i) {
    // Cost to transform source[0..i] to empty goal: (i+1) deletions
    currRow[0] = deletionCost_ * static_cast<double>(i + 1);

    for (size_t j = 0; j < goal_.size(); ++j) {
      double deleteCost = prevRow[j + 1] + deletionCost_;  // Delete source[i]
      double insertCost = currRow[j] + 1.0;                // Insert goal[j]
      double replaceCost = prevRow[j] + (source[i] == goal_[j] ? 0.0 : 1.0);

      currRow[j + 1] = std::min({deleteCost, insertCost, replaceCost});
    }

    // Cache this row for future queries with this prefix
    // Only cache every few rows to balance memory vs. speed
    if ((i + 1) % cacheInterval_ == 0 || i == source.size() - 1) {
      size_t h = hashPrefix(source, i + 1);
      prefixCache_[h] = currRow;
    }

    std::swap(prevRow, currRow);
  }

  return prevRow[goal_.size()];
}

void Levenshtein::clearCache() {
  prefixCache_.clear();
}

size_t Levenshtein::hashPrefix(const std::string& s, size_t len) {
  // Use standard hash with length to avoid collisions between
  // different strings with same prefix
  size_t h = std::hash<std::string_view>{}(std::string_view(s.data(), len));
  // Mix in length to distinguish "abc" prefix of "abcd" from "abc" prefix of "abcx"
  h ^= len + 0x9e3779b9 + (h << 6) + (h >> 2);
  return h;
}

