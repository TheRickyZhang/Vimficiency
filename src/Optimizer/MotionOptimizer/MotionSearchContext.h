#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "MotionOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "Effort/EffortBank.h"
#include "Types/NavContext.h"
#include "Types/InclusiveCharRange.h"
#include "Types/CursorPos.h"
#include "Optimizer/MotionOptimizer/MotionState.h"
#include "Types/Pos.h"
#include "Types/Lines.h"

// MotionSearchContext encapsulates shared state and logic for motion optimization search.
// Used by both optimize() (single goal) and optimizeToRange() (range goal).
struct MotionSearchContext {
  // References to external data
  const Lines& lines;
  const NavContext& navContext;
  const MotionBoundary& boundary;
  const MotionOptimizerParams& params;
  const Config& config;

  // Pre-computed effort for static KeyedSequence constants
  EffortBank bank;

  // A* priority weights from params
  double effortWeight;
  double distanceWeight;

  // Search state
  using PriorityQueue = std::priority_queue<MotionState, std::vector<MotionState>, std::greater<MotionState>>;
  PriorityQueue pq;
  std::unordered_map<Pos, double> costMap;
  int nodesProcessed = 0;   // Non-stale states processed (diagnostic metric)
  int totalPops = 0;        // All pops including stale (hard budget)
  int motionsEmitted = 0;   // Total motions generated (for stats)
  int statesSkipped = 0;    // States skipped due to staleness
  double maxEffort;  // userEffort * exploreFactor

  // Debug: optionally track explored states
  std::vector<ExploredState> exploredStates;

  // Constructor
  MotionSearchContext(const Lines& lines,
                      const NavContext& navContext,
                      const MotionBoundary& boundary,
                      const MotionOptimizerParams& params,
                      const Config& config,
                      double userEffort);

  // ==========================================================================
  // Distance and priority computation
  // ==========================================================================

  // Manhattan distance to a single goal position
  double distanceToGoal(CursorPos pos, CursorPos goal) const {
    return std::abs(goal.line - pos.line) + std::abs(goal.targetCol - pos.targetCol);
  }

  // Distance to closest point in the inclusive target set [firstPos, lastPos].
  // Goal membership is defined over valid landing positions, not half-open text geometry.
  double distanceToRange(CursorPos pos, const InclusiveCharRange& range) const {
    if (range.contains(pos)) {
      return 0.0;  // Inside range
    }
    Pos closest = (pos < range.firstPos) ? range.firstPos : range.lastPos;
    return std::abs(closest.line - pos.line) + std::abs(closest.col - pos.targetCol);
  }

  // Compute A* priority: effortWeight * effort + distanceWeight * distance
  double computePriority(double effort, double distance) const {
    return effortWeight * effort + distanceWeight * distance;
  }

  // Convenience: compute priority for single goal
  double computePriorityToGoal(const MotionState& s, CursorPos goal) const {
    return computePriority(s.getEffort(), distanceToGoal(s.getPos(), goal));
  }

  // Convenience: compute priority for inclusive target set [firstPos, lastPos].
  double computePriorityToRange(const MotionState& s,
                                const InclusiveCharRange& range) const {
    return computePriority(
        s.getEffort(),
        distanceToRange(s.getPos(), range));
  }

  // ==========================================================================
  // State management
  // ==========================================================================

  // Add state to priority queue if it improves on existing cost.
  // goalKey: the goal position key (not cached to allow multiple results)
  void exploreNewState(MotionState&& state, const Pos& goalKey);

  // Variant for inclusive target ranges: positions in the target set are not cached.
  void exploreNewStateToRange(MotionState&& state, const InclusiveCharRange& range);

  // Check if search should continue
  bool shouldContinue() const {
    if (pq.empty()) return false;
    if (totalPops >= params.maxNodesPopped) return false;
    return true;
  }

  // Pop next state from queue (caller must call markProcessed() for non-stale states)
  MotionState popNext() {
    MotionState s = pq.top();
    pq.pop();
    totalPops++;
    return s;
  }

  // Call after processing a non-stale state (including goals)
  void markProcessed() { nodesProcessed++; }

  // Check if state is stale (superseded by better path)
  bool isStale(const MotionState& s) const {
    auto it = costMap.find(s.getKey());
    return it != costMap.end() && it->second < s.getCost();
  }

  // Track an explored state (call from main loop when trackExploredStates is true)
  void trackState(const MotionState& s) {
    if (params.trackExploredStates) {
      CursorPos pos = s.getPos();
      exploredStates.push_back({pos.line, pos.col, s.getEffort(), s.getSequence().str()});
    }
  }

  // Get search stats - call after search completes
  MotionSearchStats getStats(int resultsFound) const {
    MotionSearchStats stats;
    stats.nodesExplored = nodesProcessed;
    stats.totalPops = totalPops;
    stats.resultsFound = resultsFound;
    stats.queueSizeAtStop = static_cast<int>(pq.size());
    stats.motionsEmitted = motionsEmitted;
    stats.statesSkipped = statesSkipped;
    stats.exploredStates = exploredStates;  // Copy if tracking was enabled

    if (resultsFound >= params.maxResults) {
      stats.stopReason = SearchStopReason::MaxResultsFound;
    } else if (totalPops >= params.maxNodesPopped) {
      stats.stopReason = SearchStopReason::MaxPopsReached;
    } else if (pq.empty()) {
      stats.stopReason = SearchStopReason::FullyExplored;
    }
    return stats;
  }
};
