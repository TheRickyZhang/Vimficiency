#pragma once

#include <ostream>
#include <sstream>
#include <vector>
#include <string>

#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "RunningEffort.h"
#include "Utils/Lines.h"

// =============================================================================
// EditStateKey - for visited state tracking in A* search
// =============================================================================

struct EditStateKey {
  Lines lines;
  int line;
  int col;
  Mode mode;

  EditStateKey(const Lines& l, Position p, Mode m = Mode::Normal)
      : lines(l), line(p.line), col(p.col), mode(m) {}

  bool operator==(const EditStateKey& other) const {
    return line == other.line && col == other.col
        && mode == other.mode && lines == other.lines;
  }
};

struct EditStateKeyHash {
  size_t operator()(const EditStateKey& k) const {
    size_t h = 0;
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    // Hash first line content for differentiation
    if (!k.lines.empty()) {
      h ^= std::hash<std::string>{}(k.lines[0]) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    h ^= std::hash<size_t>{}(k.lines.size()) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

// =============================================================================
// EditState - A* search state for edit optimization
// =============================================================================

struct EditState {
  Lines lines;              // Current buffer content
  Position pos;             // Cursor position
  Mode mode = Mode::Normal; // Current editing mode
  RunningEffort effort;     // Typing effort tracker
  std::vector<std::string> seq;  // Sequence of operations taken
  int startIndex;           // Which starting position this search is for
  double cost;              // Priority = effort + heuristic

  // For priority queue ordering (min-heap)
  bool operator>(const EditState& other) const {
    return cost > other.cost;
  }

  bool operator<(const EditState& other) const {
    return cost < other.cost;
  }

  EditStateKey getKey() const {
    return EditStateKey(lines, pos, mode);
  }

  // Get effort value using config
  double getEffort(const Config& config) const {
    return effort.getEffort(config);
  }

  // Build sequence string from operations
  std::string getSequenceString() const {
    std::string result;
    for (const auto& op : seq) {
      result += op;
    }
    return result;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }

  friend std::ostream& operator<<(std::ostream& os, const EditState& state) {
    os << state.getSequenceString() << " (effort=" << state.cost << ")";
    return os;
  }
};

