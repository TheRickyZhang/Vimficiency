#pragma once

#include <string>
#include <unordered_map>

#include "Editor/Mode.h"
#include "Keyboard/KeyedSequence.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"

// =============================================================================
// Suffix Cache Types - for cross-position sharing in EditOptimizer
// =============================================================================

// Key for suffix cache: (linesHash, lineCount, Position, Mode) WITHOUT startIndex
// This enables sharing cached suffixes across different starting positions.
// Uses precomputed 64-bit hash instead of full buffer copy for O(1) key construction.
struct SuffixKey {
  size_t linesHash;
  int lineCount;
  int line;
  int col;
  Mode mode;

  SuffixKey(size_t lh, int lc, int ln, int c, Mode m)
      : linesHash(lh), lineCount(lc), line(ln), col(c), mode(m) {}

  SuffixKey(size_t lh, int lc, Position p, Mode m)
      : linesHash(lh), lineCount(lc), line(p.line), col(p.col), mode(m) {}

  bool operator==(const SuffixKey& other) const {
    return linesHash == other.linesHash && lineCount == other.lineCount
        && line == other.line && col == other.col
        && mode == other.mode;
  }
};

struct SuffixKeyHash {
  size_t operator()(const SuffixKey& k) const {
    size_t h = k.linesHash;
    h ^= std::hash<int>{}(k.lineCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct SuffixValue {
  KeyedSequence ks;       // Suffix command sequence + physical keys
  RunningEffort effort;   // Pre-computed effort for just the suffix

  // Dot-context fields: when the suffix originally started with '.', the first
  // dot is expanded to the explicit command for context-independent caching.
  // At lookup time, if the searcher's lastEdit matches expandedDotCmd, the dot
  // variant (lower cost) is used instead.
  std::string expandedDotCmd;  // Command that replaced '.'; empty if no expansion
  KeyedSequence dotKs;         // Original suffix with leading '.'
  RunningEffort dotEffort;     // Effort for dot variant

};

using SuffixCacheMap = std::unordered_map<SuffixKey, SuffixValue, SuffixKeyHash>;
