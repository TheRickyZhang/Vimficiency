#pragma once

#include <climits>

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// EditOptimizerParams - Parameters for EditOptimizer
// =============================================================================
// EditOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total results across all positions.
// Default is unlimited (all positions) since EditOptimizer's multi-source search
// naturally bounds by totalPositions. Callers can set a lower limit to cap search.

struct EditOptimizerParams : OptimizerParamsBase {
  // Override base default: EditOptimizer finds results for all positions by default.
  // The search is also bounded by totalPositions in shouldContinue().
  EditOptimizerParams() { maxResults = INT_MAX; }
  // Line padding for internal MotionOptimizer calls (visual delete path).
  // Lower default than CompositionOptimizer since effectiveLines already
  // includes prefix/suffix context. Adjustable to 0 for no padding.
  // See docs/optimizer/buffer-slicing.md for details.
  int motionLinePaddingAbove = 1;
  int motionLinePaddingBelow = 1;

  // Chainable setters for fluent configuration
  EditOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  EditOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  EditOptimizerParams& withMotionLinePaddingAbove(int v) { motionLinePaddingAbove = v; return *this; }
  EditOptimizerParams& withMotionLinePaddingBelow(int v) { motionLinePaddingBelow = v; return *this; }
  EditOptimizerParams& withMotionLinePadding(int v) { motionLinePaddingAbove = motionLinePaddingBelow = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static EditOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    EditOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesExplored = maxNodesExplored;
    p.distanceWeight = 0.0;
    return p;
  }
};
