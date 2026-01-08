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
  // deletionCost controls the cost of deleting source characters (default 1.0).
  // Lower values (e.g., 0.1) encourage deletion of "wrong" content while still
  // accounting for the effort needed. Setting to 0 makes deletions free but may
  // cause the heuristic to be too optimistic.
  explicit Levenshtein(std::string goal, double deletionCost = 1.0);

  // Compute Levenshtein distance from source to goal.
  // Uses cached DP rows for shared prefixes when available.
  int distance(const std::string& source) const;
  int distance(const std::vector<std::string>& lines) const;

  // Clear the prefix cache (e.g., when starting a new search)
  void clearCache();

  // Get the goal string
  const std::string& goal() const { return goal_; }

  // Set cache interval (cache a row every N rows). Default: 4.
  // Lower = more memory, faster lookups. Higher = less memory, more recomputation.
  void setCacheInterval(size_t interval) { cacheInterval_ = interval; }

  // Compute distance as a double (for fractional deletion costs)
  double distanceDouble(const std::string& source) const;

private:
  std::string goal_;
  std::vector<double> baseRow_;  // DP row for empty source string (using double for fractional costs)
  mutable std::unordered_map<size_t, std::vector<double>> prefixCache_;
  size_t cacheInterval_ = 4;
  double deletionCost_ = 1.0;  // Cost of deleting a source character

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

