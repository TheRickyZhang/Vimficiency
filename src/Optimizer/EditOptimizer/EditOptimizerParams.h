#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// EditOptimizerParams - Parameters for EditOptimizer
// =============================================================================
// EditOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total completions reaching goal state.
// Higher default than other optimizers because multi-source search produces
// duplicate completions (same position reached via different paths).
// The search also stops when all starts become terminal
// (either capped or exhausted) in shouldContinue().

struct EditOptimizerParams : OptimizerParamsBase {
  EditOptimizerParams() { maxResults = 20; }
  // Line padding for internal MotionOptimizer calls (visual delete path).
  // Lower default than CompositionOptimizer since effectiveLines already
  // includes prefix/suffix context. Adjustable to 0 for no padding.
  // See docs/optimizer/buffer-slicing.md for details.
  int motionLinePaddingAbove = 1;
  int motionLinePaddingBelow = 1;

  // Maximum number of results to keep per starting cursor position.
  // Default 1 preserves legacy behavior (single best result per start).
  int maxMultiplePerStartPosition = 1;

  // Chainable setters for fluent configuration
  EditOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  EditOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  EditOptimizerParams& withMotionLinePaddingAbove(int v) { motionLinePaddingAbove = v; return *this; }
  EditOptimizerParams& withMotionLinePaddingBelow(int v) { motionLinePaddingBelow = v; return *this; }
  EditOptimizerParams& withMotionLinePadding(int v) { motionLinePaddingAbove = motionLinePaddingBelow = v; return *this; }
  EditOptimizerParams& withMaxMultiplePerStartPosition(int v) { maxMultiplePerStartPosition = v; return *this; }
  EditOptimizerParams& withMinCountRepeat(int v) { setMinCountRepeat(v); return *this; }
  EditOptimizerParams& withMaxCountRepeat(int v) { setMaxCountRepeat(v); return *this; }
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
