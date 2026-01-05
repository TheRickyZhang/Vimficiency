#pragma once

#include <string>

#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Keyboard/MotionToKeys.h"
#include "RunningEffort.h"

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
  Position pos;
  Mode mode;

  // Index into edit sequence / line sate stored externally, 0 = no edits done
  int editsCompleted;

  std::string motionSequence;

  double effort;
  double cost;

  RunningEffort runningEffort;

public:
  CompositionState(Position pos, Mode mode, int editsCompleted,
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
  Position getPos() const { return pos; }
  Mode getMode() const { return mode; }
  int getEditsCompleted() const { return editsCompleted; }
  const std::string& getMotionSequence() const { return motionSequence; }
  double getEffort() const { return effort; }
  double getCost() const { return cost; }
  RunningEffort getRunningEffort() const { return runningEffort; }

  // ==========================================================================
  // State transitions
  // ==========================================================================

  // Apply a movement motion (doesn't change editsCompleted)
  // Caller must provide new position (computed via motion application)
  void applyMovement(const std::string& motion, const Position& newPos,
                     const KeySequence& keySequence, const Config& config) {
    pos = newPos;
    motionSequence += motion;
    effort = runningEffort.append(keySequence, config);
  }

  // Apply an edit transition (uses pre-computed EditResult)
  // - sequence: the optimal key sequence for this edit (from EditResult.adj)
  // - newPos: position after edit completes (end position in edit region)
  // - editCost: cost of this edit transition (from EditResult.adj)
  void applyEditTransition(const std::string& sequence, const Position& newPos,
                           const KeySequence& keySequence, const Config& config) {
    pos = newPos;
    editsCompleted++;
    motionSequence += sequence;
    effort = runningEffort.append(keySequence, config);
  }

  // Apply movement result from MovementOptimizer::optimizeToRange()
  // - sequence: the optimal motion sequence string
  // - newPos: position after movement completes
  void applyMovementResult(const std::string& sequence, const Position& newPos,
                           const Config& config) {
    pos = newPos;
    motionSequence += sequence;
    KeySequence keySeq = globalTokenizer().tokenize(sequence);
    effort = runningEffort.append(keySeq, config);
  }

  void updateCost(double newCost) { cost = newCost; }

  void setPos(const Position& newPos) { pos = newPos; }
  void setMode(Mode newMode) { mode = newMode; }
};
