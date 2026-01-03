#pragma once

#include <string>
#include <vector>
#include <unordered_map>

// -----------------------------------------------------------------------------
// Levenshtein Distance with Prefix Caching
// -----------------------------------------------------------------------------
//
// Future improvements (if needed):
// - Ukkonen's algorithm: O(nd) where d = edit distance
//   Faster when d is small, which is common in A* (we prioritize close states)
// - Myers' bit-vector: O(nm/64) for m <= 64, uses SIMD-like bit parallelism
//
// -----------------------------------------------------------------------------

class Levenshtein {
public:
  // Construct with a fixed goal string. The goal is what all queries compare against.
  explicit Levenshtein(std::string goal);

  // Compute Levenshtein distance from source to goal.
  // Uses cached DP rows for shared prefixes when available.
  int distance(const std::string& source) const;

  // Clear the prefix cache (e.g., when starting a new search)
  void clearCache();

  // Get the goal string
  const std::string& goal() const { return goal_; }

  // Set cache interval (cache a row every N rows). Default: 4.
  // Lower = more memory, faster lookups. Higher = less memory, more recomputation.
  void setCacheInterval(size_t interval) { cacheInterval_ = interval; }

private:
  std::string goal_;
  std::vector<int> baseRow_;  // DP row for empty source string
  mutable std::unordered_map<size_t, std::vector<int>> prefixCache_;
  size_t cacheInterval_ = 4;

  // Hash a prefix of the string for cache lookup
  static size_t hashPrefix(const std::string& s, size_t len);
};

// -----------------------------------------------------------------------------
// Multi-line buffer distance helper
// -----------------------------------------------------------------------------
//
// For edit optimization, we treat the buffer as a single string with newlines.
// This correctly models Vim operations that work on newlines:
// - `J` / `gJ`: Join lines (delete newline)
// - `o` / `O`: Open line (insert newline)
// - `dd`: Delete line including newline
//
// Example:
//   Before: "aaa\nbbb\nccc"
//   After:  "aaa\nccc"
//   Distance: 4 (delete "bbb\n")
//
// This is simpler than per-line distance which requires alignment logic
// when line counts differ.
// -----------------------------------------------------------------------------

