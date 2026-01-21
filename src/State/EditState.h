#pragma once

#include <ostream>
#include <sstream>
#include <vector>
#include <string>

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

  // For priority queue ordering (min-heap)
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

  EditStateKey getKey() const { return EditStateKey(lines, pos, mode); }
  double getEffort() const { return effort_; }
  double getCost() const { return cost_; }
  const std::string& getSeq() const { return seq_; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // -----------------------------------------------------------------------------
  // State mutation methods
  // -----------------------------------------------------------------------------

  // Apply a deletion to the buffer (does NOT update seq - use appendToSeq separately)
  void applyDeletion(const Range& range) {
    VimCore::deleteRange(lines, range, pos, Mode::Normal);
  }

  // Apply a linewise deletion (for dd - deletes entire lines including newlines)
  void applyLinewiseDeletion(int line) {
    VimCore::deleteRangeLinewise(lines, LineRange(line, line), pos);
  }

  // Apply a linewise change (for cc - clears line content, cursor stays at col 0)
  void applyLinewiseChange(int line) {
    assert(line >= 0 && line < static_cast<int>(lines.size()));
    lines[line].clear();
    pos.line = line;
    pos.col = 0;
  }

  // Append a command string to the sequence
  void appendToSeq(const char* cmd) {
    seq_ += cmd;
  }

  // Update effort with new keys
  void updateEffort(const PhysicalKeys& keys, const Config& config) {
    effort_ = runningEffort.append(keys, config);
  }

  // Update cost (typically effort + heuristic)
  void updateCost(double newCost) {
    cost_ = newCost;
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

