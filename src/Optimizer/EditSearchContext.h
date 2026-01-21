#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "Config.h"
#include "OptimizerParams.h"
#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Keyboard/EditToKeys.h"
#include "Keyboard/KeyboardModel.h"
#include "State/EditState.h"
#include "Utils/Lines.h"
#include "VimCore/VimEndpointUtils.h"

// Callback type for characterwise deletion exploration
// Called with (range, deleteCmd, deleteKeys) for each valid deletion
using DeletionCallback = std::function<void(const Range&, const char*, const PhysicalKeys&)>;

// Callback type for linewise deletion exploration (dd)
// Called with (line, deleteCmd, deleteKeys) for each valid linewise deletion
using LinewiseCallback = std::function<void(int, const char*, const PhysicalKeys&)>;

// Callback type for motion-only exploration (when cursor in boundary region)
// Called with (newPos, motionCmd, motionKeys) - no buffer change, just cursor movement
using MotionCallback = std::function<void(const Position&, const char*, const PhysicalKeys&)>;

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
  std::pair<int, int> computeEditBounds(const Lines& lines, const Position& cursor) const;

  // Compute remaining heuristic for A* search
  double heuristic(const Lines& lines) const;

  // Explore all valid deletions from current state
  // Calls onDeletion for characterwise deletions, onLinewise for full-line (dd)
  // Pass nullptr for onLinewise to skip linewise exploration
  // Pass onMotion to explore pure cursor movements when cursor is in boundary region
  void exploreAllDeletions(const EditState& state,
                           DeletionCallback onDeletion,
                           LinewiseCallback onLinewise = nullptr,
                           MotionCallback onMotion = nullptr);


  // Check if search should continue
  bool shouldContinue() const;

  // Pop next valid state from queue, skipping stale states.
  // Returns nullopt if queue becomes empty.
  std::optional<EditState> getNextValidState();

private:
  // Exploration helpers - allow passing subset vectors for limited exploration
  void exploreForwardWordEdits(
      const std::vector<Edit::ForwardWordEditSpec>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);
  void exploreBackwardWordEdits(
      const std::vector<Edit::BackwardWordEditSpec>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);
  void exploreTextObjectEdits(
      const std::vector<Edit::TextObjectEditSpec>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);
  void exploreHalfLineEdits(
      const std::vector<Edit::LineEditSpec>& specs,
      const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
      DeletionCallback onDeletion);
  void exploreFullLineEdits(
    const std::vector<Edit::FullLineEditSpec>& specs,
    const Position& cursor, const Lines& lines, LinewiseCallback onLinewise
  );
  void exploreCharEdits(
      const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
      int editContentLen, DeletionCallback onDeletion);
};
