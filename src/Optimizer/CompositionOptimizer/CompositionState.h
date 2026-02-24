#pragma once

#include <string_view>

#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/Sequence.h"
#include "Keyboard/PhysicalKeys.h"
#include "Effort/RunningEffort.h"

// =============================================================================
// CompositionStateKey: Unique identifier for deduplication in A* search
// =============================================================================
//
// Unlike EditState (which keys on lines pointer), CompositionState derives
// lines from editsCompleted index, so key is just (pos, mode, editsCompleted).

struct CompositionStateKey {
  int line;
  int col;
  Mode mode;
  int editsCompleted;

  CompositionStateKey(int line, int col, Mode mode, int editsCompleted)
      : line(line), col(col), mode(mode), editsCompleted(editsCompleted) {}

  bool operator==(const CompositionStateKey& other) const {
    return line == other.line && col == other.col &&
           mode == other.mode && editsCompleted == other.editsCompleted;
  }
};

struct CompositionStateKeyHash {
  size_t operator()(const CompositionStateKey& key) const {
    size_t h = 0;
    combine(h, key.line);
    combine(h, key.col);
    combine(h, static_cast<int>(key.mode));
    combine(h, key.editsCompleted);
    return h;
  }

private:
  template <typename T>
  static void combine(size_t& h, const T& v) {
    h ^= std::hash<T>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
  }
};


class CompositionState {
  CursorPos pos;
  Mode mode;

  // Index into edit sequence / line state stored externally, 0 = no edits done
  int editsCompleted;

  // Concatenated command sequence
  Sequence sequence;

  double effort;
  double cost;

  RunningEffort runningEffort;

  // Helper to append to sequence and update effort
  void appendSequence(std::string_view s, const PhysicalKeys& keys, const Config& config);

public:
  CompositionState(CursorPos pos, Mode mode, int editsCompleted,
                   RunningEffort runningEffort = RunningEffort(),
                   double effort = 0.0, double cost = 0.0)
      : pos(pos), mode(mode), editsCompleted(editsCompleted),
        runningEffort(runningEffort), effort(effort), cost(cost) {}

  // Comparison for priority queue (min-heap by cost)
  bool operator<(const CompositionState& other) const { return cost < other.cost; }
  bool operator>(const CompositionState& other) const { return cost > other.cost; }

  // Key for deduplication
  CompositionStateKey getKey() const {
    return CompositionStateKey(pos.line, pos.col, mode, editsCompleted);
  }

  // Accessors
  CursorPos getPos() const { return pos; }
  Mode getMode() const { return mode; }
  int getEditsCompleted() const { return editsCompleted; }

  // Get sequence
  const Sequence& getSequence() const { return sequence; }

  double getEffort() const { return effort; }
  double getCost() const { return cost; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // ==========================================================================
  // State transitions - return new state with transition applied
  // ==========================================================================

  // Create new state with edit transition applied
  // - editSequence: the sequence for this edit (from EditResult)
  // - newPos: position after edit completes (end position in edit region)
  // - newMode: mode after edit completes
  // Note: caller must set cost via setCost() after computing heuristic
  [[nodiscard]] CompositionState afterEditTransition(
      const Sequence& editSequence,
      const CursorPos& newPos, Mode newMode,
      const Config& config) const;

  // Create new state with motion result applied
  // - moveSequence: the sequence for this movement
  // - newPos: position after movement completes
  // Note: caller must set cost via setCost() after computing heuristic
  [[nodiscard]] CompositionState afterMotionResult(
      const Sequence& moveSequence,
      const CursorPos& newPos,
      const Config& config) const;

  void setCost(double newCost) { cost = newCost; }

private:
  // Internal: apply edit transition to this state (mutates)
  void applyEditTransitionImpl(const Sequence& editSequence,
                               const CursorPos& newPos, Mode newMode,
                               const Config& config);

  // Internal: apply motion result to this state (mutates)
  void applyMotionResultImpl(const Sequence& moveSequence,
                             const CursorPos& newPos, const Config& config);
};
