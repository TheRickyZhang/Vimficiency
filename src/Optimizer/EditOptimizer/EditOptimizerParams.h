#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// EditOptimizerParams - Parameters for EditOptimizer
// =============================================================================
// EditOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total completions reaching goal state.
// Higher default than other optimizers because multi-source search produces
// duplicate completions (same position reached via different paths).
// The search is also implicitly bounded by totalPositions (unique starting
// positions covered) in shouldContinue().

struct EditOptimizerParams : OptimizerParamsBase {
  EditOptimizerParams() { maxResults = 20; }
  // Line padding for internal MotionOptimizer calls (visual delete path).
  // Lower default than CompositionOptimizer since effectiveLines already
  // includes prefix/suffix context. Adjustable to 0 for no padding.
  // See docs/optimizer/buffer-slicing.md for details.
  int motionLinePaddingAbove = 1;
  int motionLinePaddingBelow = 1;

  // Cognitive overhead penalty for counted commands (e.g., counting characters for {n}x).
  // Added to effort for each counted edit explored. Default 0.0 (disabled).
  double countOverhead = 0.0;

  // Chainable setters for fluent configuration
  EditOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  EditOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  EditOptimizerParams& withMotionLinePaddingAbove(int v) { motionLinePaddingAbove = v; return *this; }
  EditOptimizerParams& withMotionLinePaddingBelow(int v) { motionLinePaddingBelow = v; return *this; }
  EditOptimizerParams& withMotionLinePadding(int v) { motionLinePaddingAbove = motionLinePaddingBelow = v; return *this; }
  EditOptimizerParams& withMinCountRepeat(int v) { minCountRepeat = v; return *this; }
  EditOptimizerParams& withCountOverhead(double v) { countOverhead = v; return *this; }
  EditOptimizerParams& withTrackExploredStates(bool v) { trackExploredStates = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static EditOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    EditOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesExplored = maxNodesExplored;
    p.distanceWeight = 0.0;
    return p;
  }
};
