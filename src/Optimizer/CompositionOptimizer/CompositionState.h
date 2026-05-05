#pragma once

#include <string_view>
#include <utility>

#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/Sequence.h"
#include "Keyboard/Config.h"
#include "Keyboard/PhysicalKeys.h"
#include "Effort/RunningEffort.h"

// =============================================================================
// CompositionStateKey: Unique identifier for deduplication in A* search
// =============================================================================
//
// Unlike TransformStateKey, which includes a TransformEditorState content hash,
// CompositionState derives lines from editsCompleted, so key is just
// (pos, mode, editsCompleted).

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

// Ranked composition search state. CompositionStateFactory is the public
// creation and transition boundary, so cost stays tied to semantic state.
class CompositionState {
  template<class CostFn>
  friend class CompositionStateFactory;

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

  CompositionState(CursorPos pos, Mode mode, int editsCompleted,
                   RunningEffort runningEffort = RunningEffort(),
                   double effort = 0.0, double cost = 0.0)
      : pos(pos), mode(mode), editsCompleted(editsCompleted),
        runningEffort(std::move(runningEffort)), effort(effort), cost(cost) {}

public:
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

private:
  // Internal: apply edit transition to this state (mutates)
  void applyEditTransitionImpl(const Sequence& editSequence,
                               const CursorPos& newPos, Mode newMode,
                               const Config& config);

  // Internal: apply motion result to this state (mutates)
  void applyNavResultImpl(const Sequence& moveSequence,
                             const CursorPos& newPos, const Config& config);
};

template<class CostFn>
class CompositionStateFactory {
  const Config& config;
  CostFn computeCost;

  double costFor(const CompositionState& state) const {
    return computeCost(state);
  }

public:
  CompositionStateFactory(const Config& config, CostFn computeCost)
      : config(config), computeCost(std::move(computeCost)) {}

  [[nodiscard]] CompositionState initial(
      CursorPos pos, Mode mode = Mode::Normal,
      int editsCompleted = 0,
      RunningEffort runningEffort = RunningEffort()) const {
    double effort = runningEffort.getEffort(config);
    CompositionState state(pos, mode, editsCompleted, std::move(runningEffort), effort, 0.0);
    state.cost = costFor(state);
    return state;
  }

  [[nodiscard]] CompositionState afterEditTransition(
      const CompositionState& state,
      const Sequence& editSequence,
      const CursorPos& newPos, Mode newMode) const {
    CompositionState newState = state;
    newState.applyEditTransitionImpl(editSequence, newPos, newMode, config);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] CompositionState afterNavResult(
      const CompositionState& state,
      const Sequence& moveSequence,
      const CursorPos& newPos) const {
    CompositionState newState = state;
    newState.applyNavResultImpl(moveSequence, newPos, config);
    newState.cost = costFor(newState);
    return newState;
  }
};

template<class CostFn>
CompositionStateFactory(const Config&, CostFn) -> CompositionStateFactory<CostFn>;
