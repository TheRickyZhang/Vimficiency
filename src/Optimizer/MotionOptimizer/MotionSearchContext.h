#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "Optimizer/Config.h"
#include "MotionOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "Keyboard/KeyedSequenceBank.h"
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
  const MotionOptimizerParams& params;
  const Config& config;

  // Pre-computed KeyedSequences with embedded effort (O(1) merge in hot path)
  KeyedSequenceBank bank;

  // A* priority weights from params
  double effortWeight;
  double distanceWeight;

  // Search state
  using PriorityQueue = std::priority_queue<MotionState, std::vector<MotionState>, std::greater<MotionState>>;
  PriorityQueue pq;
  std::unordered_map<PosKey, double, PosKeyHash> costMap;
  int nodesProcessed = 0;   // Non-stale states processed (user-facing limit)
  int totalPops = 0;        // All pops including stale (internal safety)
  int motionsEmitted = 0;   // Total motions generated (for stats)
  int statesSkipped = 0;    // States skipped due to staleness
  double maxEffort;  // userEffort * exploreFactor

  // Internal safety: hard cap on total pops to prevent runaway loops
  // If >90% of pops are stale, something is pathologically wrong
  static constexpr int SAFETY_MULTIPLIER = 10;

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
  double distanceToGoal(Position pos, Position goal) const {
    return std::abs(goal.line - pos.line) + std::abs(goal.targetCol - pos.targetCol);
  }

  // Distance to closest point in a range [rangeFirst, rangeEnd)
  double distanceToRange(Position pos, Position rangeFirst, Position rangeEnd) const {
    if (pos >= rangeFirst && pos < rangeEnd) {
      return 0.0;  // Inside range
    }
    // For distance computation, use the last inclusive position
    Position closest = (pos < rangeFirst) ? rangeFirst : Position(rangeEnd.line, rangeEnd.col - 1);
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

  // Convenience: compute priority for range goal [first, end)
  double computePriorityToRange(const MotionState& s, Position first, Position end) const {
    return computePriority(s.getEffort(), distanceToRange(s.getPos(), first, end));
  }

  // ==========================================================================
  // State management
  // ==========================================================================

  // Add state to priority queue if it improves on existing cost.
  // goalKey: the goal position key (not cached to allow multiple results)
  void exploreNewState(MotionState&& state, const PosKey& goalKey);

  // Variant for range goals: positions in range are not cached
  void exploreNewStateToRange(MotionState&& state, Position rangeFirst, Position rangeEnd);

  // Check if search should continue
  bool shouldContinue() const {
    if (pq.empty()) return false;
    if (nodesProcessed >= params.maxNodesExplored) return false;
    // Safety cap: prevent runaway loops if too many stale nodes
    if (totalPops >= params.maxNodesExplored * SAFETY_MULTIPLIER) return false;
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
      Position pos = s.getPos();
      exploredStates.push_back({pos.line, pos.col, s.getMotionSequence()});
    }
  }

  // Get search stats - call after search completes
  SearchStats getStats(int resultsFound) const {
    SearchStats stats;
    stats.nodesExplored = nodesProcessed;
    stats.resultsFound = resultsFound;
    stats.queueSizeAtStop = static_cast<int>(pq.size());
    stats.motionsEmitted = motionsEmitted;
    stats.statesSkipped = statesSkipped;
    stats.exploredStates = exploredStates;  // Copy if tracking was enabled

    if (resultsFound >= params.maxResults) {
      stats.stopReason = SearchStopReason::MaxResultsFound;
    } else if (nodesProcessed >= params.maxNodesExplored) {
      stats.stopReason = SearchStopReason::MaxNodesReached;
    } else if (pq.empty()) {
      stats.stopReason = SearchStopReason::FullyExplored;
    }
    return stats;
  }
};
