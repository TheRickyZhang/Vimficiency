#pragma once

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

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
  size_t linesHash;
  int lineCount;
  int line;
  int col;
  Mode mode;
  int startIndex;  // Include startIndex so each starting position has independent search

  EditStateKey(size_t lh, int lc, Position p, Mode m, int idx)
      : linesHash(lh), lineCount(lc), line(p.line), col(p.col), mode(m), startIndex(idx) {}

  bool operator==(const EditStateKey& other) const {
    return linesHash == other.linesHash && lineCount == other.lineCount
        && line == other.line && col == other.col
        && mode == other.mode && startIndex == other.startIndex;
  }
};

struct EditStateKeyHash {
  size_t operator()(const EditStateKey& k) const {
    size_t h = k.linesHash;
    h ^= std::hash<int>{}(k.lineCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.startIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
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
  size_t linesHash_;              // Precomputed FNV-1a hash of buffer content

  std::string seq_{};             // Sequence of operations taken
  RunningEffort runningEffort{};  // Typing effort tracker (internal)
  double effort_ = 0.0;           // Cached effort value
  double cost_ = 0.0;             // Priority = effort + heuristic

public:
  EditState(Lines lines, Position pos, int startIndex, double initialCost)
    : lines(std::move(lines)), pos(pos), startIndex(startIndex),
      linesHash_(hashLines(this->lines)), cost_(initialCost) {}

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
  size_t getLinesHash() const { return linesHash_; }

  EditStateKey getKey() const { return EditStateKey(linesHash_, static_cast<int>(lines.size()), pos, mode, startIndex); }
  double getEffort() const { return effort_; }
  double getCost() const { return cost_; }
  const std::string& getSeq() const { return seq_; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // Record a goal-reaching command with real effort in cost.
  // Unlike recordSearch (which uses an externally-computed cost that lags behind
  // the real effort), this computes cost = effortWeight * realEffort, giving
  // correct PQ ordering for goal states (distance heuristic = 0 at goal).
  // Sets mode to Insert so the costMap key is distinct from intermediate
  // Normal-mode states at the same (buffer, position).
  void recordGoalSearch(std::string_view cmd, const PhysicalKeys& keys,
                        double effortWeight, const Config& config) {
    seq_ += cmd;
    effort_ = runningEffort.append(keys, config);
    cost_ = effortWeight * effort_;
    mode = Mode::Insert;
  }

  // -----------------------------------------------------------------------------
  // State transitions - return new state with buffer mutation applied
  // These do NOT record the command - use recordSearch() separately
  // -----------------------------------------------------------------------------

  // Create new state with deletion applied
  [[nodiscard]] EditState afterDeletion(const Range& range) const {
    EditState newState = *this;
    VimCore::deleteRange(newState.lines, range, newState.pos, Mode::Normal);
    newState.linesHash_ = hashLines(newState.lines);
    return newState;
  }

  // Create new state with linewise deletion applied (for dd)
  [[nodiscard]] EditState afterLinewiseDeletion(int line) const {
    EditState newState = *this;
    VimCore::deleteRangeLinewise(newState.lines, LineRange(line, line), newState.pos);
    newState.linesHash_ = hashLines(newState.lines);
    return newState;
  }

  // Create new state with join applied (J/gJ)
  [[nodiscard]] EditState afterJoin(bool addSpace) const {
    EditState newState = *this;
    VimCore::joinLines(newState.lines, newState.pos, addSpace);
    newState.linesHash_ = hashLines(newState.lines);
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

