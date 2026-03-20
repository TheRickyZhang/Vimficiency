#pragma once

#include <cassert>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CompositionOptimizerParams.h"
#include "DiffState.h"
#include "JoinPlan.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/MotionOptimizer/BufferIndex.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/MotionBoundary.h"
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
  const MotionBoundary& boundary;

  // Per-edit bundled data (one entry per diff, indexed 0..totalEdits()-1)
  struct PerEditData {
    DiffState diffState;

    EditResult editResult;

    std::optional<JoinPlan> joinPlan;
    BracketQuoteContext bracketQuoteContext;

    // Motion-search acceleration index over a subset of the pre-edit buffer.
    // Covers lines [bufferIndexStart, bufferIndexEnd) of the pre-edit buffer.
    BufferIndex bufferIndex;
    int bufferIndexStart = 0;
    int bufferIndexEnd = 0;
  };
  std::vector<PerEditData> edits;

  int totalEdits() const { return static_cast<int>(edits.size()); }

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
  int totalPops = 0;  // Hard budget: all queue pops, including stale.
  int statesSkipped = 0;

  // Sub-optimizer aggregate stats
  int motionNodesExplored = 0;
  int editNodesExplored = 0;

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
      std::string_view userSequence,
      const NavContext& navContext,
      const MotionBoundary& boundary,
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

  // Get a reusable pre-computed BufferIndex for a given edit level, adjusted for
  // the motion search window. On failure, clears out-params and returns false so
  // callers can fall back to the overload that builds a local index.
  bool tryGetBufferIndex(
      int editsCompleted, int motionBeginLine, int motionEndLine,
      const BufferIndex*& outIndex, int& outLineOffset) const;

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
      CursorPos pos = s.getPos();
      exploredStates.push_back({{pos.line, pos.col, s.getEffort(), s.getSequence().str()},
                                s.getEditsCompleted()});
    }
  }

  std::vector<CompositionExploredState> takeExploredStates() {
    return std::move(exploredStates);
  }

  // Build CompositionSearchStats from current context state
  CompositionSearchStats getStats(int resultsFound) const;

private:
  // Fencepost vectors (size = totalEdits() + 1): represent states *between* edits
  std::vector<Lines> linesAfterNEdits_;
  std::vector<double> suffixEditCosts_;

  // Helper: compute suffix sums of median edit costs
  std::vector<double> computeSuffixEditCosts() const;

  // Helper: solve each edit region independently, populates edits[i].editResult
  void calculateEditResults();

  // Helper: build intermediate buffer states after each diff
  // Non-const: adjusts edits[i].diffState positions from original-buffer to intermediate-buffer coordinates
  std::vector<Lines> calculateLinesAfterDiffs(const Lines& initialLines);

  // Helper: compute text object contexts for each edit, populates edits[i].bracketQuoteContext
  void computeTextObjectContexts();

  // Helper: compute J (join lines) plans for diffs where source has more lines than target, populates edits[i].joinPlan
  void computeJoinPlans();

};
