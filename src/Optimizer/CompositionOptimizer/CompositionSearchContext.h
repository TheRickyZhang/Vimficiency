#pragma once

#include <cassert>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "CompositionOptimizerParams.h"
#include "DiffState.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "Keyboard/MotionToKeys.h"
#include "State/CompositionState.h"
#include "Utils/Lines.h"

// =============================================================================
// CompositionSearchContext
// =============================================================================
// Encapsulates shared state and logic for composition optimization search.
// Handles:
// - Pre-computed diff states and edit results
// - A* priority queue and cost map for state exploration
// - Heuristic computation with asymmetric overshoot penalty

struct CompositionSearchContext {
  // References to external data
  const Config& config;
  const CompositionOptimizerParams& params;
  const NavContext& navContext;
  const MotionBoundary& boundary;

  // Filtered motion keys (gg/G removed based on boundary)
  MotionToKeys motionToKeys;

  // Processing direction (true = forward/left-to-right)
  bool forward;

  // Pre-computed diff data
  std::vector<DiffState> diffStates;
  int totalEdits;

  // Pre-computed edit solutions (one EditResult per diff, including pure insertions)
  std::vector<EditResult> editResults;

  // Intermediate buffer states: linesAfterNEdits[i] = buffer after i edits applied
  // linesAfterNEdits[0] = initialLines, linesAfterNEdits[totalEdits] = goalLines
  std::vector<Lines> linesAfterNEdits;

  // Suffix sums of median edit costs for O(1) heuristic lookup
  // suffixEditCosts[i] = sum of median costs for edits i..totalEdits-1
  std::vector<double> suffixEditCosts;

  // Heuristic tuning parameters
  double overshootPenalty;
  double forwardBias;
  int maxLineLength;

  // A* priority weights from params
  double effortWeight;
  double distanceWeight;

  // Search state
  using PriorityQueue = std::priority_queue<CompositionState, std::vector<CompositionState>,
                                            std::greater<CompositionState>>;
  PriorityQueue pq;
  std::unordered_map<CompositionStateKey, double, CompositionStateKeyHash> costMap;

  // Search limits
  double maxEffort;  // userEffort * exploreFactor

  // Stats tracking
  int nodesProcessed = 0;
  int totalPops = 0;
  int statesSkipped = 0;

  // Internal safety: hard cap on total pops to prevent runaway loops
  static constexpr int SAFETY_MULTIPLIER = 10;

  // ==========================================================================
  // Construction
  // ==========================================================================

  // Constructor - performs all pre-computation
  CompositionSearchContext(
      const Lines& initialLines,
      const Position& initialPos,
      const Lines& goalLines,
      const std::string& userSequence,
      const NavContext& navContext,
      const MotionBoundary& boundary,
      const MotionToKeys& rawMotionToKeys,
      const CompositionOptimizerParams& params,
      const Config& config,
      double overshootPenalty,
      double forwardBias,
      int maxLineLength);

  // ==========================================================================
  // Position conversion helpers
  // ==========================================================================

  // Convert flat index within edit region's insertedLines to buffer position
  Position editIndexToBufferPos(int flatIndex, const DiffState& diff) const;

  // ==========================================================================
  // Heuristic and distance computation
  // ==========================================================================

  // Manhattan distance between two positions
  double costToGoal(const Position& curr, const Position& goal) const {
    return std::abs(goal.line - curr.line) + std::abs(goal.col - curr.col);
  }

  // h(n) for A*: estimates remaining cost
  // Uses suffix sum of median edit costs + asymmetric distance to next edit region
  double heuristic(const CompositionState& s, int editsCompleted) const;

  // ==========================================================================
  // State management
  // ==========================================================================

  // Add state to priority queue if it improves on existing cost
  void exploreNewState(CompositionState&& newState);

  // Check if search should continue
  bool shouldContinue() const {
    if (pq.empty()) return false;
    if (nodesProcessed >= params.maxNodesExplored) return false;
    // Safety cap: prevent runaway loops if too many stale nodes
    if (totalPops >= params.maxNodesExplored * SAFETY_MULTIPLIER) return false;
    return true;
  }

  // Pop next state from queue
  CompositionState popNext() {
    CompositionState s = pq.top();
    pq.pop();
    totalPops++;
    return s;
  }

  // Call after processing a non-stale state
  void markProcessed() { nodesProcessed++; }

  // Check if state is stale (superseded by better path)
  bool isStale(const CompositionState& s) const {
    auto it = costMap.find(s.getKey());
    return it != costMap.end() && it->second < s.getCost();
  }

  // Check if this is a goal state
  bool isGoal(const CompositionState& s) const {
    return s.getEditsCompleted() == totalEdits;
  }

  // Get buffer state for a given number of completed edits
  const Lines& getLinesAfter(int editsCompleted) const {
    return linesAfterNEdits[editsCompleted];
  }

  // Check if the edit at this index is a pure insertion
  bool isPureInsertion(int editIndex) const {
    return diffStates[editIndex].isPureInsertion();
  }

  // Get the diff state for an edit index
  const DiffState& getDiffState(int editIndex) const {
    return diffStates[editIndex];
  }

  // ==========================================================================
  // Stats
  // ==========================================================================

  // Build SearchStats from current context state
  SearchStats getStats(int resultsFound) const;

private:
  // Helper: compute suffix sums of median edit costs
  std::vector<double> computeSuffixEditCosts() const;

  // Helper: solve each edit region independently
  std::vector<EditResult> calculateEditResults();

  // Helper: build intermediate buffer states after each diff
  std::vector<Lines> calculateLinesAfterDiffs(const Lines& initialLines) const;
};
