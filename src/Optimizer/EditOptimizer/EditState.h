#pragma once

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "Keyboard/Config.h"
#include "Types/LineRange.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/Range.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "Keyboard/PhysicalKeys.h"
#include "VimCore/VimCore.h"
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

  EditStateKey(size_t lh, int lc, CursorPos p, Mode m, int idx)
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
  CursorPos pos;                   // Cursor position
  Mode mode = Mode::Normal;       // Current editing mode
  int startIndex;                 // Which starting position this search is for
  size_t linesHash_;              // Precomputed FNV-1a hash of buffer content

  std::string seq_{};             // Sequence of operations taken
  int lastEditCount_ = 0;         // Count prefix of last edit for dot repeat (0 = uncounted)
  std::string lastEditBase_{};    // Base command of last edit for dot repeat (not in EditStateKey)
  RunningEffort runningEffort{};  // Typing effort tracker (internal)
  double effort_ = 0.0;           // Cached effort value
  double cost_ = 0.0;             // Priority = effort + heuristic

public:
  EditState(Lines lines, CursorPos pos, int startIndex, double initialCost)
    : lines(std::move(lines)), pos(pos), startIndex(startIndex),
      linesHash_(hashLines(this->lines)), cost_(initialCost) {}

  // For priority queue ordering (min-heap) - default uses cost (A*)
  bool operator>(const EditState& other) const { return cost_ > other.cost_; }
  bool operator<(const EditState& other) const { return cost_ < other.cost_; }

  // -----------------------------------------------------------------------------
  // Getters
  // -----------------------------------------------------------------------------
  const Lines& getLines() const { return lines; }
  CursorPos getPos() const { return pos; }
  void setPos(CursorPos newPos) { pos = newPos; }
  Mode getMode() const { return mode; }
  int getStartIndex() const { return startIndex; }
  size_t getLinesHash() const { return linesHash_; }

  EditStateKey getKey() const { return EditStateKey(linesHash_, static_cast<int>(lines.size()), pos, mode, startIndex); }
  double getEffort() const { return effort_; }
  double getCost() const { return cost_; }
  const std::string& getSeq() const { return seq_; }
  int getLastEditCount() const { return lastEditCount_; }
  const std::string& getLastEditBase() const { return lastEditBase_; }
  bool hasLastEdit() const { return !lastEditBase_.empty(); }
  void setLastEdit(int count, std::string_view base) { lastEditCount_ = count; lastEditBase_ = base; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // -----------------------------------------------------------------------------
  // State transitions - return new state with buffer mutation applied
  // These do NOT record the command - use recordSearch() separately
  // -----------------------------------------------------------------------------

  // Create new state with deletion applied
  [[nodiscard]] EditState afterDeletion(const Range& range) const {
    EditState newState = *this;
    VimCore::deleteRangeAndUpdatePos(newState.lines, range, newState.pos, Mode::Normal);
    newState.linesHash_ = hashLines(newState.lines);
    return newState;
  }

  // Create new state with db/dB deletion semantics applied, including the
  // special post-delete cursor adjustment after removing the begin line.
  [[nodiscard]] EditState afterBackwardWordDeletion(const Range& range) const {
    EditState newState = *this;
    Range normalized = range;
    normalized.normalize();
    int oldLineCount = static_cast<int>(newState.lines.size());
    CursorPos originalPos = newState.pos;

    VimCore::deleteRangeAndUpdatePos(newState.lines, normalized, newState.pos, Mode::Normal);

    if (originalPos.col == 0
        && originalPos.line > normalized.begin.line
        && VimCore::didDeleteRangeRemoveBeginLine(
               normalized, oldLineCount, static_cast<int>(newState.lines.size()))
        && !newState.lines[newState.pos.line].empty()) {
      newState.pos.setCol(VimCore::firstNonBlankColInLineStr(newState.lines[newState.pos.line]));
    }

    newState.linesHash_ = hashLines(newState.lines);
    return newState;
  }

  // Create new state with linewise deletion applied (for dd)
  // hasLinesBelow: if true, cursor is not clamped when deleting the last line
  [[nodiscard]] EditState afterLinewiseDeletion(int line, bool hasLinesBelow = false) const {
    EditState newState = *this;
    VimCore::deleteLineRangeAndUpdatePos(
        newState.lines, LineRange(line, line + 1), newState.pos, hasLinesBelow);
    newState.linesHash_ = hashLines(newState.lines);
    return newState;
  }

  // Create new state with multi-line linewise deletion applied (for dj, dk, {n}dd)
  // hasLinesBelow: if true, cursor is not clamped when deleting the last line
  [[nodiscard]] EditState afterMultiLinewiseDeletion(LineRange range, bool hasLinesBelow = false) const {
    EditState newState = *this;
    VimCore::deleteLineRangeAndUpdatePos(newState.lines, range, newState.pos, hasLinesBelow);
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

  // Create new state with multiple joins applied ({n}J, {n}gJ: joins count lines)
  [[nodiscard]] EditState afterMultiJoin(int count, bool addSpace) const {
    EditState newState = *this;
    for (int i = 0; i < count - 1; i++) {
      VimCore::joinLines(newState.lines, newState.pos, addSpace);
    }
    newState.linesHash_ = hashLines(newState.lines);
    return newState;
  }

  // -----------------------------------------------------------------------------
  // Command recording
  // -----------------------------------------------------------------------------

  // Record a search command: appends to sequence, updates effort, computes cost.
  // Cost = effortWeight * actualEffort + heuristicCost (heuristicCost = 0 at goal).
  void recordSearch(std::string_view cmd, const PhysicalKeys& keys,
                    double effortWeight, double heuristicCost, const Config& config) {
    seq_ += cmd;
    effort_ = runningEffort.append(keys, config);
    cost_ = effortWeight * effort_ + heuristicCost;
  }

  // Overload using pre-computed effort (from effort cache) — avoids recomputing from keys.
  void recordSearch(std::string_view cmd, const RunningEffort& precomputed,
                    double effortWeight, double heuristicCost, const Config& config) {
    seq_ += cmd;
    effort_ = runningEffort.appendFrom(precomputed, config);
    cost_ = effortWeight * effort_ + heuristicCost;
  }

  // Counted overload: prepends count digits to base command in sequence string.
  // count=0 means uncounted (base command only).
  void recordSearch(int count, std::string_view baseCmd, const RunningEffort& precomputed,
                    double effortWeight, double heuristicCost, const Config& config) {
    if (count > 0) seq_ += std::to_string(count);
    seq_ += baseCmd;
    effort_ = runningEffort.appendFrom(precomputed, config);
    cost_ = effortWeight * effort_ + heuristicCost;
  }

  void recordSearch(int count, std::string_view baseCmd, const PhysicalKeys& keys,
                    double effortWeight, double heuristicCost, const Config& config) {
    if (count > 0) seq_ += std::to_string(count);
    seq_ += baseCmd;
    effort_ = runningEffort.append(keys, config);
    cost_ = effortWeight * effort_ + heuristicCost;
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
