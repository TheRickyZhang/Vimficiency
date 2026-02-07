#pragma once

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/LineRange.h"
#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Optimizer/Config.h"
#include "RunningEffort.h"
#include "Utils/Lines.h"
#include "Keyboard/KeyboardModel.h"
#include "VimCore/VimEditUtils.h"

// =============================================================================
// EditStateKey - for visited state tracking in A* search
// =============================================================================

struct EditStateKey {
  Lines lines;
  int line;
  int col;
  Mode mode;
  int startIndex;  // Include startIndex so each starting position has independent search

  EditStateKey(const Lines& l, Position p, Mode m, int idx)
      : lines(l), line(p.line), col(p.col), mode(m), startIndex(idx) {}

  bool operator==(const EditStateKey& other) const {
    return line == other.line && col == other.col
        && mode == other.mode && startIndex == other.startIndex
        && lines == other.lines;
  }
};

struct EditStateKeyHash {
  size_t operator()(const EditStateKey& k) const {
    size_t h = 0;
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.startIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
    // Hash first line content for differentiation (Lines invariant: always at least one line)
    h ^= std::hash<std::string>{}(k.lines[0]) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<size_t>{}(k.lines.size()) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

// =============================================================================
// EditState - A* search state for edit optimization
// =============================================================================

class EditState {
  Lines lines;                    // Current buffer content
  Position pos;                   // Cursor position
  Mode mode = Mode::Normal;       // Current editing mode
  int startIndex;                 // Which starting position this search is for

  std::string seq_{};             // Sequence of operations taken
  RunningEffort runningEffort{};  // Typing effort tracker (internal)
  double effort_ = 0.0;           // Cached effort value
  double cost_ = 0.0;             // Priority = effort + heuristic

public:
  EditState(Lines lines, Position pos, int startIndex, double initialCost)
    : lines(std::move(lines)), pos(pos), startIndex(startIndex), cost_(initialCost) {}

  // For priority queue ordering (min-heap) - default uses cost (A*)
  bool operator>(const EditState& other) const { return cost_ > other.cost_; }
  bool operator<(const EditState& other) const { return cost_ < other.cost_; }

  // -----------------------------------------------------------------------------
  // Getters
  // -----------------------------------------------------------------------------
  const Lines& getLines() const { return lines; }
  Position getPos() const { return pos; }
  void setPos(Position newPos) { pos = newPos; }
  Mode getMode() const { return mode; }
  int getStartIndex() const { return startIndex; }

  EditStateKey getKey() const { return EditStateKey(lines, pos, mode, startIndex); }
  double getEffort() const { return effort_; }
  double getCost() const { return cost_; }
  const std::string& getSeq() const { return seq_; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // -----------------------------------------------------------------------------
  // State transitions - return new state with buffer mutation applied
  // These do NOT record the command - use recordSearch() separately if not goal
  // -----------------------------------------------------------------------------

  // Create new state with deletion applied
  [[nodiscard]] EditState afterDeletion(const Range& range) const {
    EditState newState = *this;
    VimCore::deleteRange(newState.lines, range, newState.pos, Mode::Normal);
    return newState;
  }

  // Create new state with linewise deletion applied (for dd)
  [[nodiscard]] EditState afterLinewiseDeletion(int line) const {
    EditState newState = *this;
    VimCore::deleteRangeLinewise(newState.lines, LineRange(line, line), newState.pos);
    return newState;
  }

  // Create new state with join applied (J/gJ)
  [[nodiscard]] EditState afterJoin(bool addSpace) const {
    EditState newState = *this;
    VimCore::joinLines(newState.lines, newState.pos, addSpace);
    return newState;
  }

  // -----------------------------------------------------------------------------
  // Command recording - for continued search (not goal states)
  // -----------------------------------------------------------------------------

  // Record a search command: appends to sequence, updates effort and cost
  void recordSearch(std::string_view cmd, const PhysicalKeys& keys,
                    double cost, const Config& config) {
    seq_ += cmd;
    effort_ = runningEffort.append(keys, config);
    cost_ = cost;
  }

  // -----------------------------------------------------------------------------
  // Debug/Output
  // -----------------------------------------------------------------------------
  std::string toString() const {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }

  friend std::ostream& operator<<(std::ostream& os, const EditState& state) {
    os << state.seq_ << " (cost=" << state.cost_ << ")";
    return os;
  }
};

