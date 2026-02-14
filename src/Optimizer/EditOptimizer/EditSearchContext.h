#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "Optimizer/Config.h"
#include "EditOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/EditBoundary.h"
#include "Editor/LineRange.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Keyboard/KeyedSequence.h"
#include "State/EditState.h"
#include "Utils/Lines.h"

// Forward declaration
class EditExplorer;

// Callback types (also defined in EditExplorer.h for standalone use)
// Each callback receives (count, baseKS, effort). count=0 for uncounted commands.
// MotionCallback is unchanged — motions don't go through exploreWithDot or dot repeat.
using DeletionCallback = std::function<void(const Range&, int count, const KeyedSequence& baseKS, const RunningEffort&)>;
using LinewiseCallback = std::function<void(int line, int count, const KeyedSequence& baseKS, const RunningEffort&)>;
using MotionCallback = std::function<void(const Position&, const KeyedSequence&, const RunningEffort&)>;
using JoinCallback = std::function<void(bool addSpace, int count, const KeyedSequence& baseKS, const RunningEffort&)>;

// Counted operation callbacks
using CountedLinewiseCallback = std::function<void(LineRange, int count, const KeyedSequence& baseKS, const RunningEffort&)>;
using CountedJoinCallback = std::function<void(int count, bool addSpace, const KeyedSequence& baseKS, const RunningEffort&)>;

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

  // Pre-computed RunningEffort for static KeyedSequence constants (address-keyed).
  // Populated at construction for all X-macro entries and spec table entries.
  // Use effortFor() to look up — only valid for known static KS (asserts on miss).
  std::unordered_map<const void*, RunningEffort> effortCache_;
  RunningEffort periodEffort_;

  // Search state
  using PriorityQueue = std::priority_queue<EditState, std::vector<EditState>, EditStateComparator>;
  PriorityQueue pq;
  std::unordered_map<EditStateKey, double, EditStateKeyHash> costMap;
  int resultsFound = 0;            // Total completions reaching goal (consistent with other optimizers)
  int uniquePositionsCovered = 0;  // Unique starting positions that have a result
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

  // Explore a state via normal path (move) and optionally dot path (copy).
  // Checks dot eligibility first and copies only when needed.
  // Normal path always moves afterState and sets lastEdit to (count, baseCmd).
  void exploreWithDot(EditState&& afterState, const EditState& base,
                      int count, const KeyedSequence& baseKS,
                      const RunningEffort& effort, double hCost);

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

  // Heuristic cost component (for recordSearch's heuristicCost parameter)
  double heuristicCost(const Lines& lines) const {
    return distanceWeight * distanceHeuristic(lines);
  }

  // Look up pre-computed effort for a known static KeyedSequence (by address).
  // Only call for X-macro KS constants or spec table entries — asserts on miss.
  const RunningEffort& effortFor(const KeyedSequence& ks) const {
    auto it = effortCache_.find(&ks);
    assert(it != effortCache_.end() && "effortFor called with unknown KeyedSequence");
    return it->second;
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

  // Explore counted line edits (dj, dk, {n}dd) from current state
  void exploreCountedLineEdits(const EditState& state, CountedLinewiseCallback cb);

  // Explore counted join commands ({n}J, {n}gJ) from current state
  void exploreCountedJoinCommands(const EditState& state, CountedJoinCallback cb);

  // Explore counted word edits ({n}de, {n}dw, etc.) from current state
  void exploreCountedWordEdits(const EditState& state, DeletionCallback cb);

  // Explore counted char edits ({n}x) from current state
  void exploreCountedCharEdits(const EditState& state, DeletionCallback cb);


  // Convert startIndex to seed position in effectiveLines coordinates.
  // Reverses the flat indexing used by initStartingPositions.
  Position seedPositionFor(int startIndex, const Lines& initialLines) const {
    int remaining = startIndex;
    for (int r = 0; r < static_cast<int>(initialLines.size()); r++) {
      int lineSize = initialLines[r].empty() ? 1 : static_cast<int>(initialLines[r].size());
      if (remaining < lineSize) {
        int effCol = remaining + (r == 0 ? leftColOffset : 0);
        return Position(r, effCol);
      }
      remaining -= lineSize;
    }
    return Position(-1, -1);  // Invalid
  }

  // Check if search should continue
  bool shouldContinue() const;

  // Pop next valid state from queue, skipping stale states.
  // Returns nullopt if queue becomes empty.
  std::optional<EditState> getNextValidState();

  // Build SearchStats from current context state
  SearchStats getStats() const;
};
