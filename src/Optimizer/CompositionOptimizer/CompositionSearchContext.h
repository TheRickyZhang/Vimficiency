#pragma once

#include <cassert>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "CompositionOptimizerParams.h"
#include "DiffState.h"
#include "JoinPlan.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "VimTypes/NavContext.h"
#include "VimTypes/Position.h"
#include "Optimizer/CompositionOptimizer/CompositionState.h"
#include "VimTypes/BracketFlags.h"
#include "VimTypes/Lines.h"
#include "VimTypes/QuoteFlags.h"

// =============================================================================
// TextObjectContext
// =============================================================================
// Per-edit tracking of valid text object entry points.
// For each column on the edit's line, tracks which quote/bracket types
// can be used as text objects to reach the edit region from that column.

struct BracketQuoteContext {
  // For quotes: validQuoteMask[col] has bits set for quote types valid from col.
  // A quote type is valid from col if:
  //   1. No prior quote of that type exists on this line (would pair with earlier)
  //   2. A valid quote pair of that type contains the edit region
  std::vector<QuoteFlags> validQuoteMask;  // Indexed by column

  // For brackets: validBracketMask[col] has bits set for bracket types valid from col.
  // A bracket type is valid from col if:
  //   1. Balance is 0 at col (not inside a pair of that type)
  //   2. The next forward pair of that type contains the edit region
  std::vector<BracketFlags> validBracketMask;  // Indexed by column

  // Whether to use around (true) or inner (false) for each delimiter type
  // Set based on whether edit region includes delimiters
  QuoteFlags useAroundQuote;
  BracketFlags useAroundBracket;

  // Line this context applies to (-1 if not applicable, e.g., pure insertion)
  int line = -1;

  // Explicit default constructor to ensure proper initialization
  BracketQuoteContext() : validQuoteMask(), validBracketMask(), useAroundQuote(), useAroundBracket(), line(-1) {}

  bool hasAnyValid() const {
    for (const auto& mask : validQuoteMask) {
      if (mask.seen('"') || mask.seen('\'') || mask.seen('`')) return true;
    }
    for (const auto& mask : validBracketMask) {
      if (mask.seen('(') || mask.seen('[') || mask.seen('{') || mask.seen('<')) return true;
    }
    return false;
  }

  char quoteModifier(char q) const {
    return useAroundQuote.seen(q) ? 'a' : 'i';
  }
  char bracketModifier(char b) const {
    return useAroundBracket.seen(b) ? 'a' : 'i';
  }
};

// =============================================================================
// CompositionExploredState
// =============================================================================
// Extends ExploredState with composition-specific tracking.

struct CompositionExploredState : ExploredState {
  int editsCompleted;
};

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


  // Pre-computed diff data
  std::vector<DiffState> diffStates;
  int totalEdits;

  // Pre-computed edit solutions (one EditResult per diff, including pure insertions)
  std::vector<EditResult> editResults;

  // For pure-deletion diffs only: per-start cursor landing positions after the
  // selected edit sequence (same flat indexing as editResults[i].getResults()).
  // Empty for non-pure-deletion diffs.
  std::vector<std::vector<Position>> pureDeletionGoalPosByEdit;

  // Pre-computed J (join lines) plans: one per diff, present when J is viable
  std::vector<std::optional<JoinPlan>> joinPlans;

  // Intermediate buffer states: linesAfterNEdits[i] = buffer after i edits applied
  // linesAfterNEdits[0] = initialLines, linesAfterNEdits[totalEdits] = goalLines
  std::vector<Lines> linesAfterNEdits;

  // Text object shortcut contexts: one per edit, tracks valid quote/bracket entry points
  // textObjectContexts[i] applies to edit i, using buffer linesAfterNEdits[i]
  std::vector<BracketQuoteContext> bracketQuoteContexts;

  // Suffix sums of median edit costs for O(1) heuristic lookup
  // suffixEditCosts[i] = sum of median costs for edits i..totalEdits-1
  std::vector<double> suffixEditCosts;

  // Heuristic tuning parameters
  double overshootPenalty;
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

  // Sub-optimizer aggregate stats
  int motionNodesExplored = 0;
  int editNodesExplored = 0;

  // Debug: optionally track explored states (composition-specific)
  std::vector<CompositionExploredState> exploredStates;

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
      std::string_view userSequence,
      const NavContext& navContext,
      const MotionBoundary& boundary,
      const CompositionOptimizerParams& params,
      const Config& config);

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
  // State exploration - single entry points for state transitions
  // ==========================================================================

  // Explore an edit transition: create state, compute cost, add to queue
  // editsAfter = editsCompleted after this edit (editsCompleted + 1)
  void exploreEditTransition(const CompositionState& current,
                             const Sequence& editSequence,
                             const Position& goalPos,
                             int editsAfter);

  // Explore a motion transition: create state, compute cost, add to queue
  void exploreMotionTransition(const CompositionState& current,
                               const Sequence& moveSequence,
                               const Position& goalPos,
                               int editsCompleted);

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

  // Get the diff state for an edit index
  const DiffState& getDiffState(int editIndex) const {
    return diffStates[editIndex];
  }

  // ==========================================================================
  // Stats
  // ==========================================================================

  // Track an explored state (call from main loop when trackExploredStates is true)
  void trackState(const CompositionState& s) {
    if (params.trackExploredStates) {
      Position pos = s.getPos();
      exploredStates.push_back({{pos.line, pos.col, s.getEffort(), s.getSequence().str()},
                                s.getEditsCompleted()});
    }
  }

  // Build SearchStats from current context state
  SearchStats getStats(int resultsFound) const;

private:
  // Internal: add state to priority queue if it improves on existing cost
  void exploreNewState(CompositionState&& newState);

  // Helper: compute suffix sums of median edit costs
  std::vector<double> computeSuffixEditCosts() const;

  // Helper: solve each edit region independently
  std::vector<EditResult> calculateEditResults();

  // Helper: build intermediate buffer states after each diff
  // Non-const: adjusts diffStates positions from original-buffer to intermediate-buffer coordinates
  std::vector<Lines> calculateLinesAfterDiffs(const Lines& initialLines);

  // Helper: compute text object contexts for each edit
  std::vector<BracketQuoteContext> computeTextObjectContexts() const;

  // Helper: compute J (join lines) plans for diffs where source has more lines than target
  std::vector<std::optional<JoinPlan>> computeJoinPlans();

};
