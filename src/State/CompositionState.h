#pragma once

#include <string>
#include <vector>

#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "RunningEffort.h"
#include "Sequence.h"

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

  // Index into edit sequence / line state stored externally, 0 = no edits done
  int editsCompleted;

  // Command sequences grouped by mode (for display output)
  std::vector<Sequence> sequences;

  double effort;
  double cost;

  RunningEffort runningEffort;

  // Helper to append to the appropriate mode segment
  void appendSequence(const std::string& s, const PhysicalKeys& keys, const Config& config) {
    if (sequences.empty() || sequences.back().mode != mode) {
      sequences.emplace_back(mode);
    }
    sequences.back().append(s);
    effort = runningEffort.append(keys, config);
  }

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

  // Get sequences grouped by mode
  const std::vector<Sequence>& getSequences() const { return sequences; }

  // Get flattened string representation
  std::string getMotionSequence() const { return flattenSequences(sequences); }

  double getEffort() const { return effort; }
  double getCost() const { return cost; }
  RunningEffort getRunningEffort() const { return runningEffort; }

  // ==========================================================================
  // State transitions
  // ==========================================================================

  // Apply a movement motion (doesn't change editsCompleted)
  // Caller must provide new position (computed via motion application)
  void applyMovement(const std::string& motion, const Position& newPos,
                     const PhysicalKeys& keys, const Config& config) {
    pos = newPos;
    appendSequence(motion, keys, config);
  }

  // Apply an edit transition (uses pre-computed EditResult)
  // - editSequences: the sequences for this edit (from EditResult.adj)
  // - newPos: position after edit completes (end position in edit region)
  // - newMode: mode after edit completes
  void applyEditTransition(const std::vector<Sequence>& editSequences,
                           const Position& newPos, Mode newMode,
                           const Config& config) {
    pos = newPos;
    editsCompleted++;
    // Merge edit sequences into our sequences
    for (const auto& seq : editSequences) {
      // Set mode to match each segment's mode before appending
      mode = seq.mode;
      PhysicalKeys keys = globalTokenizer().tokenize(seq.keys);
      appendSequence(seq.keys, keys, config);
    }
    mode = newMode;
  }

  // Apply movement result from MovementOptimizer::optimizeToRange()
  // - moveSequences: the sequences for this movement
  // - newPos: position after movement completes
  void applyMovementResult(const std::vector<Sequence>& moveSequences,
                           const Position& newPos, const Config& config) {
    pos = newPos;
    for (const auto& seq : moveSequences) {
      PhysicalKeys keys = globalTokenizer().tokenize(seq.keys);
      appendSequence(seq.keys, keys, config);
    }
  }

  void updateCost(double newCost) { cost = newCost; }

  void setPos(const Position& newPos) { pos = newPos; }
  void setMode(Mode newMode) { mode = newMode; }
};
