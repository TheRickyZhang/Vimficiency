#pragma once

#include <cassert>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CompositionOptimizerParams.h"
#include "DiffState.h"
#include "JoinPlan.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/NavBoundary.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Optimizer/CompositionOptimizer/CompositionState.h"
#include "Types/BracketFlags.h"
#include "Types/Lines.h"
#include "Types/QuoteFlags.h"

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
  const NavBoundary& boundary;

  // Final cursor target. Search terminates when editsCompleted == totalEdits()
  // AND pos == goalPos. Used by heuristic() to score post-final-edit nav cost.
  CursorPos goalPos;

  // Per-edit bundled data (one entry per diff, indexed 0..totalEdits()-1)
  struct PerEditData {
    DiffState diffState;

    TransformResult transformResult;

    std::optional<JoinPlan> joinPlan;
    BracketQuoteContext bracketQuoteContext;

  };
  std::vector<PerEditData> edits;

  int totalEdits() const { return static_cast<int>(edits.size()); }

  // Heuristic tuning parameters
  double overshootPenalty;
  int maxLineLength;

  // A* priority weights from params
  double effortWeight;
  double distanceWeight;

  // Search state.
  // pq lives locally in optimizeImpl() because its element type depends on
  // the trace policy (see QueueEntry<Trace> in CompositionOptimizer.cpp).
  // costMap stays here since its key (line, col, mode, editsCompleted) is
  // independent of trace.
  std::unordered_map<CompositionStateKey, double, CompositionStateKeyHash> costMap;

  // Search limits
  double maxEffort;  // userEffort * exploreFactor

  // Stats tracking
  int nodesProcessed = 0;
  int totalPops = 0;  // Hard budget: all queue pops, including stale.
  int statesSkipped = 0;

  // Sub-optimizer aggregate stats
  int navNodesExplored = 0;
  int editNodesExplored = 0;

  // Search-summary metrics deposited by `optimizeImpl` just before its local
  // priority queue goes out of scope; read by `buildCompositionResult`. Only
  // valid between those two points within a single optimize call.
  int lastPqRemaining = 0;
  bool lastFullyExplored = false;

  // Debug: optionally track explored states (composition-specific)
  std::vector<CompositionExploredState> exploredStates;

  // ==========================================================================
  // Construction
  // ==========================================================================

  // Constructor - performs all pre-computation
  CompositionSearchContext(
      const Lines& initialLines,
      const CursorPos& initialPos,
      const Lines& goalLines,
      const CursorPos& goalPos,
      std::string_view userSequence,
      const NavContext& navContext,
      const NavBoundary& boundary,
      const CompositionOptimizerParams& params,
      const Config& config);

  // ==========================================================================
  // CursorPos conversion helpers
  // ==========================================================================

  // Convert flat index within edit region's insertedLines to buffer position
  CursorPos editIndexToBufferPos(int flatIndex, const DiffState& diff) const;

  // ==========================================================================
  // Heuristic and distance computation
  // ==========================================================================

  // Manhattan distance between two positions
  double costToGoal(const CursorPos& curr, const CursorPos& goal) const {
    return std::abs(goal.line - curr.line) + std::abs(goal.col - curr.col);
  }

  // h(n) for A*: estimates remaining cost
  // Uses suffix sum of median edit costs + asymmetric distance to next edit region
  double heuristic(const CompositionState& s, int editsCompleted) const;

  // Get buffer state for a given number of completed edits (0..totalEdits())
  const Lines& getLinesAfter(int editsCompleted) const {
    return linesAfterNEdits_[editsCompleted];
  }

  // Get the diff state for an edit index
  const DiffState& getDiffState(int editIndex) const {
    return edits[editIndex].diffState;
  }

  // ==========================================================================
  // Stats
  // ==========================================================================

  // Track an explored state when search-trace collection is compiled in.
  void trackState(const CompositionState& s) {
    if constexpr (SEARCH_TRACE_STATS_ENABLED) {
      if (exploredStates.size() >= static_cast<size_t>(MAX_TRACED_STATES)) return;
      CursorPos pos = s.getPos();
      exploredStates.push_back({{pos.line, pos.col, s.getEffort(), s.getSequence().str()},
                                s.getEditsCompleted()});
    }
  }

  std::vector<CompositionExploredState> takeExploredStates() {
    return std::move(exploredStates);
  }

  // Build CompositionSearchStats from current context state.
  // `pqRemaining` is the size of the templated priority queue (owned by
  // optimizeImpl, not by the context); pass 0 if the search exited via
  // result/budget caps. `fullyExplored` records whether the queue drained
  // before any cap was hit.
  CompositionSearchStats getStats(int resultsFound,
                                  int pqRemaining,
                                  bool fullyExplored) const;

private:
  // Fencepost vectors (size = totalEdits() + 1): represent states *between* edits
  std::vector<Lines> linesAfterNEdits_;
  std::vector<double> suffixEditCosts_;

  // Helper: compute suffix sums of median edit costs
  std::vector<double> computeSuffixEditCosts() const;

  // Helper: solve each edit region independently, populates edits[i].transformResult
  void calculateTransformResults();

  // Helper: build intermediate buffer states after each diff.
  // Non-const: maps each planned diff into its pre-edit buffer context.
  std::vector<Lines> calculateLinesAfterDiffs(const Lines& initialLines);

  // Helper: compute text object contexts for each edit, populates edits[i].bracketQuoteContext
  void computeTextObjectContexts();

  // Helper: compute J (join lines) plans for diffs where source has more lines than target, populates edits[i].joinPlan
  void computeJoinPlans();

};
