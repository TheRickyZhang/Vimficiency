#include "Levenshtein.h"

#include <algorithm>
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


Levenshtein::Levenshtein(std::string goal)
  : goal_(std::move(goal))
{
  // Initialize base row: distance from empty string to goal[0..j]
  baseRow_.resize(goal_.size() + 1);
  for (size_t j = 0; j <= goal_.size(); ++j) {
    baseRow_[j] = static_cast<int>(j);
  }
}

int Levenshtein::distance(const std::vector<std::string>& lines) const {
    return distance(join(lines));
}

int Levenshtein::distance(const std::string& source) const {
  if (source == goal_) return 0;
  if (source.empty()) return static_cast<int>(goal_.size());
  if (goal_.empty()) return static_cast<int>(source.size());

  // Find longest cached prefix
  size_t cachedPrefixLen = 0;
  const std::vector<int>* startRow = &baseRow_;

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
  std::vector<int> prevRow = *startRow;
  std::vector<int> currRow(goal_.size() + 1);

  for (size_t i = cachedPrefixLen; i < source.size(); ++i) {
    currRow[0] = static_cast<int>(i + 1);

    for (size_t j = 0; j < goal_.size(); ++j) {
      int deleteCost = prevRow[j + 1] + 1;
      int insertCost = currRow[j] + 1;
      int replaceCost = prevRow[j] + (source[i] == goal_[j] ? 0 : 1);

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

