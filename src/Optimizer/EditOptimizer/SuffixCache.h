#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Editor/Mode.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/KeyedSequence.h"
#include "State/EditState.h"
#include "Utils/Lines.h"

// =============================================================================
// Suffix Cache Types - for cross-position sharing in EditOptimizer
// =============================================================================

// Key for suffix cache: (Lines, Position, Mode) WITHOUT startIndex
// This enables sharing cached suffixes across different starting positions
struct SuffixKey {
  Lines lines;
  int line;
  int col;
  Mode mode;

  SuffixKey(const Lines& l, int ln, int c, Mode m)
      : lines(l), line(ln), col(c), mode(m) {}

  SuffixKey(const Lines& l, Position p, Mode m)
      : lines(l), line(p.line), col(p.col), mode(m) {}

  bool operator==(const SuffixKey& other) const {
    return line == other.line && col == other.col
        && mode == other.mode && lines == other.lines;
  }
};

struct SuffixKeyHash {
  size_t operator()(const SuffixKey& k) const {
    size_t h = 0;
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    // Hash first line content (Lines invariant: always at least one line)
    h ^= std::hash<std::string>{}(k.lines[0]) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<size_t>{}(k.lines.size()) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct SuffixValue {
  KeyedSequence ks;
};

using SuffixCacheMap = std::unordered_map<SuffixKey, SuffixValue, SuffixKeyHash>;

// Committed state: stored when a state is popped from the priority queue
// Used to walk backward and populate suffix cache when a goal is reached
struct CommittedState {
  SuffixKey key;
  int parentCommitIdx;         // -1 for root (seed) states
  std::string transitionSeq;   // Command from parent to here
  PhysicalKeys transitionKeys;

  CommittedState(SuffixKey k, int parentIdx, std::string seq, PhysicalKeys keys)
      : key(std::move(k)), parentCommitIdx(parentIdx),
        transitionSeq(std::move(seq)), transitionKeys(std::move(keys)) {}
};

// Parent info: tracks cheapest parent for each EditStateKey during exploration
struct ParentInfo {
  int commitIdx;               // Index into committedStates vector
  std::string transitionSeq;   // Command from parent to this state
  PhysicalKeys transitionKeys;
  double cost;                 // Cost of reaching this state via this parent

  bool operator>(const ParentInfo& other) const { return cost > other.cost; }
};

using ParentMap = std::unordered_map<EditStateKey, ParentInfo, EditStateKeyHash>;
