#pragma once

#include <functional>
#include <queue>
#include <cstdint>
#include <unordered_map>

#include "Keyboard/Config.h"
#include "EditOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/EditBoundary.h"
#include "Types/CharLineRange.h"
#include "Types/LineRange.h"
#include "Types/LineCharRange.h"
#include "Types/CursorPos.h"
#include "Types/CharRange.h"
#include "Keyboard/KeyedSequence.h"
#include "Effort/EffortBank.h"
#include "Optimizer/EditOptimizer/EditState.h"
#include "Optimizer/SequenceBinding.h"
#include "Types/Lines.h"

// Forward declaration
class EditExplorer;

// Callback types (also defined in EditExplorer.h for standalone use)
// Each callback receives a fully bound command payload:
// (count, base command, associated effort).
using DeletionCallback = std::function<void(const CharRange&, const SequenceBinding&)>;
using CharLineDeletionCallback = std::function<void(const CharLineRange&, const SequenceBinding&)>;
using LineCharDeletionCallback = std::function<void(const LineCharRange&, const SequenceBinding&)>;
using LinewiseCallback = std::function<void(LineRange, const SequenceBinding&)>;
using MotionCallback = std::function<void(const CursorPos&, const SequenceBinding&)>;
using JoinCallback = std::function<void(bool addSpace, const SequenceBinding&)>;

// Counted operation callbacks
using CountedLinewiseCallback = std::function<void(LineRange, const SequenceBinding&)>;
using CountedJoinCallback = std::function<void(bool addSpace, const SequenceBinding&)>;

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
struct EditSearchContext {
  enum class StartStatus : uint8_t {
    Active = 0,
    Capped,
    Exhausted
  };

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

  // Pre-computed effort for all static KeyedSequence constants (KSId indexed).
  EffortBank bank;

  // Search state
  using PriorityQueue = std::priority_queue<EditState, std::vector<EditState>, EditStateComparator>;
  PriorityQueue pq;
  std::unordered_map<EditStateKey, double, EditStateKeyHash> costMap;
  int resultsFound = 0;            // Total completions reaching goal (consistent with other optimizers)
  int uniquePositionsCovered = 0;  // Unique starting positions that have a result
  int iterations = 0;
  int totalPositions;
  std::vector<int> pendingByStart;
  std::vector<StartStatus> statusByStart;
  int terminalStarts = 0;

  // Stats tracking
  int motionsEmitted = 0;
  int statesSkipped = 0;
  int totalPops = 0;  // Hard budget: counts all pops including stale

  // Debug: optionally track explored states
  std::vector<ExploredState> exploredStates;


  // Constructor - sets up context from start lines and boundary
  EditSearchContext(const Lines& initialLines,
                    const EditBoundary& boundary,
                    const EditOptimizerParams& params,
                    const Config& config);

  // Check if position is in protected boundary region
  bool inBoundaryRegion(const CursorPos& pos, const Lines& lines) const;

  // If cursor is in boundary region, explore escape motions and safe backward
  // word edits from first suffix col. Returns true if handled (caller should
  // skip further edit exploration), false if cursor is in normal content.
  bool exploreBoundaryEscape(const EditState& state,
                             DeletionCallback onDeletion,
                             MotionCallback onMotion);

  // Add state to priority queue if it improves on existing cost
  void exploreNewState(EditState&& state);

  bool isStartActive(int startIndex) const {
    return statusByStart[static_cast<size_t>(startIndex)] == StartStatus::Active;
  }

  void markStartCapped(int startIndex);
  void maybeMarkStartExhausted(int startIndex);

  // Explore a state via normal path (move) and optionally dot path (copy).
  // Checks dot eligibility first and copies only when needed.
  // Normal path always moves afterState and sets lastEdit to (count, baseCmd).
  void exploreWithDot(EditState&& afterState, const EditState& base,
                      const SequenceBinding& sourceCmd, double hCost);

  // Initialize priority queue with all starting positions
  void initStartingPositions(const Lines& initialLines);

  // Within a line, get columns that bound the edit content
  // Returns (contentStart, contentEnd)
  std::pair<int, int> computeEditBounds(const Lines& lines, const CursorPos& cursor) const;

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

  // Look up pre-computed effort for a static KeyedSequence constant.
  const RunningEffort& effortFor(KSId id) const {
    return bank[id];
  }

  // Explore all valid deletions from current state
  // Calls onDeletion for characterwise deletions, onLinewise for linewise deletions
  // Pass nullptr for onLinewise to skip linewise exploration
  // Pass onJoin to explore J/gJ commands when valid
  // Caller must check boundary region before calling (via exploreBoundaryEscape).
  void exploreAllDeletions(const EditState& state,
                           DeletionCallback onDeletion,
                           CharLineDeletionCallback onCharLine = nullptr,
                           LineCharDeletionCallback onLineChar = nullptr,
                           LinewiseCallback onLinewise = nullptr,
                           JoinCallback onJoin = nullptr);

  // Explore J/gJ commands from current state
  // Only valid when cursor line has a next line and joining wouldn't cross into suffix boundary
  void exploreJoinCommands(const CursorPos& cursor, const Lines& lines, JoinCallback onJoin);

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
  CursorPos seedPositionFor(int startIndex, const Lines& initialLines) const {
    int remaining = startIndex;
    for (int r = 0; r < static_cast<int>(initialLines.size()); r++) {
      int lineSize = initialLines[r].empty() ? 1 : static_cast<int>(initialLines[r].size());
      if (remaining < lineSize) {
        int effCol = remaining + (r == 0 ? leftColOffset : 0);
        return CursorPos(r, effCol);
      }
      remaining -= lineSize;
    }
    return CursorPos(-1, -1);  // Invalid
  }

  // Check if search should continue
  bool shouldContinue() const;

  // Pop next valid state from queue, skipping stale states.
  // Returns nullopt if queue becomes empty.
  std::optional<EditState> getNextValidState();

  // Track an explored state (call from main loop when trackExploredStates is true)
  void trackState(const EditState& s) {
    if (params.trackExploredStates) {
      CursorPos pos = s.getPos();
      exploredStates.push_back({pos.line, pos.col, s.getEffort(), s.getSeq()});
    }
  }

  // Build EditSearchStats from current context state
  EditSearchStats getStats() const;
};
