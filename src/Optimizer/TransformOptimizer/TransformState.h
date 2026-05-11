#pragma once

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "Keyboard/Config.h"
#include "Types/CharLineRange.h"
#include "Types/LineCharRange.h"
#include "Types/LineRange.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/CharRange.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"

// =============================================================================
// TransformStateKey - for visited state tracking in A* search
// =============================================================================

struct TransformStateKey {
  size_t linesHash;
  int lineCount;
  int line;
  int col;
  Mode mode;
  int startIndex;  // Include startIndex so each starting position has independent search

  TransformStateKey(size_t lh, int lc, CursorPos p, Mode m, int idx)
      : linesHash(lh), lineCount(lc), line(p.line), col(p.col), mode(m), startIndex(idx) {}

  bool operator==(const TransformStateKey& other) const {
    return linesHash == other.linesHash && lineCount == other.lineCount
        && line == other.line && col == other.col
        && mode == other.mode && startIndex == other.startIndex;
  }
};

struct TransformStateKeyHash {
  size_t operator()(const TransformStateKey& k) const {
    size_t h = k.linesHash;
    h ^= std::hash<int>{}(k.lineCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.startIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

// Editor-only snapshot: Vim buffer, cursor, mode, and cached content hash.
// Search path, effort, and queue cost are deliberately kept out.
class TransformEditorState {
  friend class TransformSimulator;

  Lines lines_;
  CursorPos pos_;
  Mode mode_ = Mode::Normal;
  size_t linesHash_ = 0;

  void refreshHash() { linesHash_ = hashLines(lines_); }

public:
  TransformEditorState(Lines lines, CursorPos pos, Mode mode = Mode::Normal)
      : lines_(std::move(lines)), pos_(pos), mode_(mode), linesHash_(hashLines(lines_)) {}

  const Lines& getLines() const { return lines_; }
  CursorPos getPos() const { return pos_; }
  Mode getMode() const { return mode_; }
  size_t getLinesHash() const { return linesHash_; }
};

// Pure editor mutations used by optimizer exploration and replay verification.
class TransformSimulator {
  static void applyDeletion(Lines& lines, const CharRange& range, CursorPos& pos) {
    VimCore::deleteRangeAndUpdatePos(lines, range, pos, Mode::Normal);
  }

  static void applyDeletion(Lines& lines, const CharLineRange& range, CursorPos& pos) {
    VimCore::deleteCharLineRangeAndUpdatePos(lines, range, pos, Mode::Normal);
  }

  static void applyDeletion(Lines& lines, const LineCharRange& range, CursorPos& pos) {
    VimCore::deleteLineCharRangeAndUpdatePos(lines, range, pos, Mode::Normal);
  }

  template<class RangeT>
  static TransformEditorState afterDeletionImpl(
      const TransformEditorState& state, const RangeT& range) {
    TransformEditorState newState = state;
    applyDeletion(newState.lines_, range, newState.pos_);
    newState.refreshHash();
    return newState;
  }

public:
  [[nodiscard]] static TransformEditorState withPos(
      const TransformEditorState& state, CursorPos newPos) {
    TransformEditorState newState = state;
    newState.pos_ = newPos;
    return newState;
  }

  [[nodiscard]] static TransformEditorState afterDeletion(
      const TransformEditorState& state, const CharRange& range) {
    return afterDeletionImpl(state, range);
  }

  [[nodiscard]] static TransformEditorState afterCharLineDeletion(
      const TransformEditorState& state, const CharLineRange& range) {
    return afterDeletionImpl(state, range);
  }

  [[nodiscard]] static TransformEditorState afterLineCharDeletion(
      const TransformEditorState& state, const LineCharRange& range) {
    return afterDeletionImpl(state, range);
  }

  [[nodiscard]] static TransformEditorState afterBackwardWordDeletion(
      const TransformEditorState& state, const CharRange& range) {
    TransformEditorState newState = state;
    CharRange normalized = range;
    normalized.normalize();
    int oldLineCount = static_cast<int>(newState.lines_.size());
    CursorPos originalPos = newState.pos_;

    VimCore::deleteRangeAndUpdatePos(newState.lines_, normalized, newState.pos_, Mode::Normal);

    if (originalPos.col == 0
        && originalPos.line > normalized.begin.line
        && VimCore::didDeleteRangeRemoveBeginLine(
               normalized, oldLineCount, static_cast<int>(newState.lines_.size()))
        && !newState.lines_[newState.pos_.line].empty()) {
      newState.pos_.setCol(
          VimCore::firstNonBlankColInLineStr(newState.lines_[newState.pos_.line]));
    }

    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] static TransformEditorState afterLinewiseDeletion(
    const TransformEditorState& state, int line, bool hasLinesBelow = false) {
    TransformEditorState newState = state;
    VimCore::deleteLineRangeAndUpdatePos(
        newState.lines_, LineRange(line, line + 1), newState.pos_, hasLinesBelow);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] static TransformEditorState afterMultiLinewiseDeletion(
      const TransformEditorState& state, LineRange range, bool hasLinesBelow = false) {
    TransformEditorState newState = state;
    VimCore::deleteLineRangeAndUpdatePos(newState.lines_, range, newState.pos_, hasLinesBelow);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] static TransformEditorState afterOperatorLinewiseDeletion(
      const TransformEditorState& state, LineRange range, bool hasLinesBelow = false) {
    TransformEditorState newState = state;
    VimCore::deleteOperatorLineRangeAndUpdatePos(
        newState.lines_, range, newState.pos_, hasLinesBelow);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] static TransformEditorState afterJoin(
      const TransformEditorState& state, bool addSpace) {
    TransformEditorState newState = state;
    VimCore::joinLines(newState.lines_, newState.pos_, addSpace);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] static TransformEditorState afterMultiJoin(
      const TransformEditorState& state, int count, bool addSpace) {
    TransformEditorState newState = state;
    for (int i = 0; i < count - 1; i++) {
      VimCore::joinLines(newState.lines_, newState.pos_, addSpace);
    }
    newState.refreshHash();
    return newState;
  }
};

inline bool usesOperatorLinewiseCursor(std::string_view baseCmd) {
  return baseCmd == "d}" || baseCmd == "d{" ||
         baseCmd == "d)" || baseCmd == "d(";
}

inline TransformEditorState afterLinewiseDeletionForCommand(
    const TransformEditorState& state, LineRange range, bool hasLinesBelow,
    std::string_view baseCmd) {
  return usesOperatorLinewiseCursor(baseCmd)
      ? TransformSimulator::afterOperatorLinewiseDeletion(state, range, hasLinesBelow)
      : TransformSimulator::afterMultiLinewiseDeletion(state, range, hasLinesBelow);
}

// =============================================================================
// TransformState - A* search state for transform optimization
// =============================================================================

// Queue-ready search state. It wraps a TransformEditorState with sequence,
// effort, cost, and dot-repeat metadata; callers advance it through
// TransformStateFactory so those fields stay synchronized.
class TransformState {
  friend class TransformStateFactory;

  TransformEditorState editor;
  int startIndex;                 // Which starting position this search is for

  std::string seq_{};             // Sequence of operations taken
  int lastEditCount_ = 0;         // Count prefix of last edit for dot repeat (0 = uncounted)
  std::string lastEditBase_{};    // Base command of last edit for dot repeat (not in TransformStateKey)
  RunningEffort runningEffort{};  // Typing effort tracker (internal)
  double effort_ = 0.0;           // Cached effort value
  double cost_ = 0.0;             // Priority = effort + heuristic

  TransformState(TransformEditorState editor, int startIndex, double initialCost)
      : editor(std::move(editor)), startIndex(startIndex), cost_(initialCost) {}

public:
  // For priority queue ordering (min-heap) - default uses cost (A*)
  bool operator>(const TransformState& other) const { return cost_ > other.cost_; }
  bool operator<(const TransformState& other) const { return cost_ < other.cost_; }

  // -----------------------------------------------------------------------------
  // Getters
  // -----------------------------------------------------------------------------
  const TransformEditorState& getEditorState() const { return editor; }
  const Lines& getLines() const { return editor.getLines(); }
  CursorPos getPos() const { return editor.getPos(); }
  Mode getMode() const { return editor.getMode(); }
  int getStartIndex() const { return startIndex; }
  size_t getLinesHash() const { return editor.getLinesHash(); }

  TransformStateKey getKey() const {
    return TransformStateKey(
        editor.getLinesHash(), static_cast<int>(editor.getLines().size()),
        editor.getPos(), editor.getMode(), startIndex);
  }
  double getEffort() const { return effort_; }
  double getCost() const { return cost_; }
  const std::string& getSeq() const { return seq_; }
  int getLastEditCount() const { return lastEditCount_; }
  const std::string& getLastEditBase() const { return lastEditBase_; }
  bool hasLastEdit() const { return !lastEditBase_.empty(); }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // -----------------------------------------------------------------------------
  // Debug/Output
  // -----------------------------------------------------------------------------
  std::string toString() const {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }

  friend std::ostream& operator<<(std::ostream& os, const TransformState& state) {
    os << state.seq_ << " (cost=" << state.cost_ << ")";
    return os;
  }
};

// Couples an editor snapshot with command effort and heuristic cost to create
// queue-ready TransformState values.
class TransformStateFactory {
  const Config& config;
  double effortWeight;

  void appendCommand(TransformState& state, std::string_view cmd,
                     const RunningEffort& precomputed,
                     double heuristicCost) const {
    state.seq_ += cmd;
    state.effort_ = state.runningEffort.appendFrom(precomputed, config);
    state.cost_ = effortWeight * state.effort_ + heuristicCost;
  }

  void appendCountedCommand(TransformState& state, int count,
                            std::string_view baseCmd,
                            const RunningEffort& precomputed,
                            double heuristicCost) const {
    if (count > 0) state.seq_ += std::to_string(count);
    state.seq_ += baseCmd;
    state.effort_ = state.runningEffort.appendFrom(precomputed, config);
    state.cost_ = effortWeight * state.effort_ + heuristicCost;
  }

public:
  TransformStateFactory(const Config& config, double effortWeight)
      : config(config), effortWeight(effortWeight) {}

  [[nodiscard]] TransformState initial(
      Lines lines, CursorPos pos, int startIndex, double initialCost) const {
    return initial(TransformEditorState(std::move(lines), pos), startIndex, initialCost);
  }

  [[nodiscard]] TransformState initial(
      TransformEditorState editor, int startIndex, double initialCost) const {
    return TransformState(std::move(editor), startIndex, initialCost);
  }

  [[nodiscard]] TransformState afterCommand(
      const TransformState& base,
      TransformEditorState editor,
      std::string_view cmd,
      const RunningEffort& precomputed,
      double heuristicCost) const {
    TransformState state = base;
    state.editor = std::move(editor);
    appendCommand(state, cmd, precomputed, heuristicCost);
    return state;
  }

  [[nodiscard]] TransformState afterEditCommand(
      const TransformState& base,
      TransformEditorState editor,
      int count,
      std::string_view baseCmd,
      const RunningEffort& precomputed,
      double heuristicCost) const {
    TransformState state = base;
    state.editor = std::move(editor);
    appendCountedCommand(state, count, baseCmd, precomputed, heuristicCost);
    state.lastEditCount_ = count;
    state.lastEditBase_ = baseCmd;
    return state;
  }

  [[nodiscard]] TransformState afterCommandWithLastEdit(
      const TransformState& base,
      TransformEditorState editor,
      std::string_view cmd,
      const RunningEffort& precomputed,
      double heuristicCost,
      int lastEditCount,
      std::string_view lastEditBase) const {
    TransformState state = afterCommand(
        base, std::move(editor), cmd, precomputed, heuristicCost);
    state.lastEditCount_ = lastEditCount;
    state.lastEditBase_ = lastEditBase;
    return state;
  }
};
