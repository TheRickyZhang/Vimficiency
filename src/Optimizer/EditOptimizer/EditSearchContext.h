#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "Optimizer/Config.h"
#include "EditOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Keyboard/KeyboardModel.h"
#include "State/EditState.h"
#include "Utils/Lines.h"

// Forward declaration
class EditExplorer;

// Callback types (also defined in EditExplorer.h for standalone use)
using DeletionCallback = std::function<void(const Range&, const char*, const PhysicalKeys&)>;
using LinewiseCallback = std::function<void(int, const char*, const PhysicalKeys&)>;
using MotionCallback = std::function<void(const Position&, const char*, const PhysicalKeys&)>;
using JoinCallback = std::function<void(bool, const char*, const PhysicalKeys&)>;

// Comparator for EditState priority queue.
// Uses getCost() which is computed as: effortWeight * effort + distanceWeight * heuristic
// - A* (default): effortWeight=1.0, distanceWeight=1.0
// - Dijkstra: effortWeight=1.0, distanceWeight=0.0 (no heuristic, optimal but slower)
// Tie-break by startIndex for fair multi-source exploration.
struct EditStateComparator {
  bool operator()(const EditState& a, const EditState& b) const {
    double aPriority = a.getCost();
    double bPriority = b.getCost();
    if (aPriority != bPriority) return aPriority > bPriority;  // min-heap
    return a.getStartIndex() > b.getStartIndex();
  }
};

// EditSearchContext encapsulates shared state and logic for edit optimization search.
// Used by both optimizeEdit and optimizePureDeletion to avoid massive code duplication.
struct EditSearchContext {
  // References to external data
  const EditBoundary& editBoundary;
  const EditOptimizerParams& params;
  const Config& config;

  // Column offsets for boundary protection (computed from editBoundary)
  int leftColOffset;
  int rightColOffset;

  // Effective lines (edit region with added prefix/suffix)
  Lines effectiveLines;

  // A* priority weights from params (priority = effortWeight * effort + distanceWeight * distance)
  double effortWeight;
  double distanceWeight;

  // Search state
  using PriorityQueue = std::priority_queue<EditState, std::vector<EditState>, EditStateComparator>;
  PriorityQueue pq;
  std::unordered_map<EditStateKey, double, EditStateKeyHash> costMap;
  int resultsFound = 0;
  int iterations = 0;
  int totalPositions;

  // Stats tracking
  int motionsEmitted = 0;
  int statesSkipped = 0;
  int totalPops = 0;  // Internal safety: counts all pops including stale

  // Internal safety: hard cap on total pops to prevent runaway loops
  // If >90% of pops are stale, something is pathologically wrong
  static constexpr int SAFETY_MULTIPLIER = 10;

  // Constructor - sets up context from start lines and boundary
  EditSearchContext(const Lines& initialLines,
                    const EditBoundary& boundary,
                    const EditOptimizerParams& params,
                    const Config& config);

  // Check if position is in protected boundary region
  bool inBoundaryRegion(const Position& pos, const Lines& lines) const;

  // Add state to priority queue if it improves on existing cost
  void exploreNewState(EditState&& state);

  // Initialize priority queue with all starting positions
  void initStartingPositions(const Lines& initialLines);

  // Within a line, get columns that bound the edit content
  // Returns (contentStart, contentEnd)
  std::pair<int, int> computeEditBounds(const Lines& lines, const Position& cursor) const;

  // Compute distance heuristic (remaining characters to delete)
  double distanceHeuristic(const Lines& lines) const;

  // Compute A* priority: effortWeight * effort + distanceWeight * distance
  double computePriority(double effort, const Lines& lines) const {
    return effortWeight * effort + distanceWeight * distanceHeuristic(lines);
  }

  // Explore all valid deletions from current state
  // Calls onDeletion for characterwise deletions, onLinewise for full-line (dd)
  // Pass nullptr for onLinewise to skip linewise exploration
  // Pass onMotion to explore pure cursor movements when cursor is in boundary region
  // Pass onJoin to explore J/gJ commands when valid
  void exploreAllDeletions(const EditState& state,
                           DeletionCallback onDeletion,
                           LinewiseCallback onLinewise = nullptr,
                           MotionCallback onMotion = nullptr,
                           JoinCallback onJoin = nullptr);

  // Explore J/gJ commands from current state
  // Only valid when cursor line has a next line and joining wouldn't cross into suffix boundary
  void exploreJoinCommands(const Position& cursor, const Lines& lines, JoinCallback onJoin);


  // Check if search should continue
  bool shouldContinue() const;

  // Pop next valid state from queue, skipping stale states.
  // Returns nullopt if queue becomes empty.
  std::optional<EditState> getNextValidState();

  // Build SearchStats from current context state
  SearchStats getStats() const;
};
