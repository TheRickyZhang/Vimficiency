#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "Config.h"
#include "OptimizerParams.h"
#include "SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "State/MotionState.h"
#include "State/PosKey.h"
#include "Utils/Lines.h"

// MotionSearchContext encapsulates shared state and logic for motion optimization search.
// Used by both optimize() (single goal) and optimizeToRange() (range goal).
struct MotionSearchContext {
  // References to external data
  const Lines& lines;
  const NavContext& navContext;
  const MotionBoundary& boundary;
  const OptimizerParams& params;
  const Config& config;

  // A* priority weights from params
  double effortWeight;
  double distanceWeight;

  // Search state
  using PriorityQueue = std::priority_queue<MotionState, std::vector<MotionState>, std::greater<MotionState>>;
  PriorityQueue pq;
  std::unordered_map<PosKey, double, PosKeyHash> costMap;
  int totalExplored = 0;
  int motionsEmitted = 0;   // Total motions generated (for stats)
  int statesSkipped = 0;    // States skipped due to staleness
  double maxEffort;  // userEffort * exploreFactor

  // Debug: optionally track explored states
  std::vector<ExploredState> exploredStates;

  // Constructor
  MotionSearchContext(const Lines& lines,
                      const NavContext& navContext,
                      const MotionBoundary& boundary,
                      const OptimizerParams& params,
                      const Config& config,
                      double userEffort);

  // ==========================================================================
  // Distance and priority computation
  // ==========================================================================

  // Manhattan distance to a single goal position
  double distanceToGoal(Position pos, Position goal) const {
    return std::abs(goal.line - pos.line) + std::abs(goal.targetCol - pos.targetCol);
  }

  // Distance to closest point in a range
  double distanceToRange(Position pos, Position rangeFirst, Position rangeLast) const {
    if (pos >= rangeFirst && pos <= rangeLast) {
      return 0.0;  // Inside range
    }
    Position closest = (pos < rangeFirst) ? rangeFirst : rangeLast;
    return distanceToGoal(pos, closest);
  }

  // Compute A* priority: effortWeight * effort + distanceWeight * distance
  double computePriority(double effort, double distance) const {
    return effortWeight * effort + distanceWeight * distance;
  }

  // Convenience: compute priority for single goal
  double computePriorityToGoal(const MotionState& s, Position goal) const {
    return computePriority(s.getEffort(), distanceToGoal(s.getPos(), goal));
  }

  // Convenience: compute priority for range goal
  double computePriorityToRange(const MotionState& s, Position first, Position last) const {
    return computePriority(s.getEffort(), distanceToRange(s.getPos(), first, last));
  }

  // ==========================================================================
  // State management
  // ==========================================================================

  // Add state to priority queue if it improves on existing cost.
  // goalKey: the goal position key (not cached to allow multiple results)
  void exploreNewState(MotionState&& state, const PosKey& goalKey);

  // Variant for range goals: positions in range are not cached
  void exploreNewStateToRange(MotionState&& state, Position rangeFirst, Position rangeLast);

  // Check if search should continue
  bool shouldContinue() const {
    return !pq.empty() && totalExplored < params.maxNodesExplored;
  }

  // Pop next state from queue (caller handles staleness check)
  MotionState popNext() {
    MotionState s = pq.top();
    pq.pop();
    totalExplored++;
    return s;
  }

  // Check if state is stale (superseded by better path)
  bool isStale(const MotionState& s) const {
    auto it = costMap.find(s.getKey());
    return it != costMap.end() && it->second < s.getCost();
  }

  // Track an explored state (call from main loop when trackExploredStates is true)
  void trackState(const MotionState& s) {
    if (params.trackExploredStates) {
      Position pos = s.getPos();
      exploredStates.push_back({pos.line, pos.col, s.getMotionSequence()});
    }
  }

  // Get search stats - call after search completes
  SearchStats getStats(int resultsFound) const {
    SearchStats stats;
    stats.nodesExplored = totalExplored;
    stats.resultsFound = resultsFound;
    stats.motionsEmitted = motionsEmitted;
    stats.statesSkipped = statesSkipped;
    stats.exploredStates = exploredStates;  // Copy if tracking was enabled

    if (resultsFound >= params.maxResults) {
      stats.stopReason = SearchStopReason::MaxResultsReached;
    } else if (totalExplored >= params.maxNodesExplored) {
      stats.stopReason = SearchStopReason::NodeLimitReached;
    } else if (pq.empty()) {
      stats.stopReason = SearchStopReason::QueueExhausted;
    }
    return stats;
  }
};
