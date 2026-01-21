#pragma once

#include <functional>
#include <queue>
#include <tuple>
#include <unordered_map>

#include "Config.h"
#include "OptimizerParams.h"
#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/EditToKeys.h"
#include "State/EditState.h"
#include "Utils/Lines.h"
#include "VimCore/VimEndpointUtils.h"

// Callback type for deletion exploration
// Called with (range, deleteCmd, deleteKeys) for each valid deletion
using DeletionCallback = std::function<void(const Range&, const char*, const PhysicalKeys&)>;

// EditSearchContext encapsulates shared state and logic for edit optimization search.
// Used by both optimizeEdit and optimizePureDeletion to avoid massive code duplication.
struct EditSearchContext {
  // References to external data
  const EditBoundary& editBoundary;
  const OptimizerParams& params;
  const Config& config;

  // Column offsets for boundary protection (computed from editBoundary)
  int leftColOffset;
  int rightColOffset;

  // Effective lines (edit region with added prefix/suffix)
  Lines effectiveLines;

  // Search state
  std::priority_queue<EditState, std::vector<EditState>, std::greater<EditState>> pq;
  std::unordered_map<EditStateKey, double, EditStateKeyHash> costMap;
  int resultsFound = 0;
  int iterations = 0;
  int totalPositions;

  // Constructor - sets up context from start lines and boundary
  EditSearchContext(const Lines& startLines,
                    const EditBoundary& boundary,
                    const OptimizerParams& params,
                    const Config& config);

  // Check if position is in protected boundary region
  bool inBoundaryRegion(const Position& pos, const Lines& lines) const;

  // Add state to priority queue if it improves on existing cost
  void exploreNewState(EditState&& state);

  // Initialize priority queue with all starting positions
  void initStartingPositions(const Lines& startLines);

  // Within a line, get columns that bound the edit content
  // Returns (contentStart, contentEnd)
  std::pair<int, int> computeContentBounds(const Lines& lines, const Position& cursor) const;

  // Compute remaining heuristic for A* search
  double heuristic(const Lines& lines) const;

  // Explore all valid characterwise deletions from current state
  // Calls callback with (range, cmd, keys) for each valid deletion
  // Does NOT handle full-line operations (dd/cc) - each optimizer handles those
  void exploreAllDeletions(const EditState& state, DeletionCallback onDeletion);

  // Check if search should continue
  bool shouldContinue() const;

  // Pop next valid state from queue, skipping stale states.
  // Returns nullopt if queue becomes empty.
  std::optional<EditState> getNextValidState();
};
