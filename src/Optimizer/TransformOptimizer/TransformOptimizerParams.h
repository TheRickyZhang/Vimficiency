#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// TransformOptimizerParams - Parameters for TransformOptimizer
// =============================================================================
// TransformOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total completions reaching goal state.
// Higher default than other optimizers because multi-source search produces
// duplicate completions (same position reached via different paths).
// The search also stops when all starts become terminal
// (either capped or exhausted) in shouldContinue().

struct TransformOptimizerParams : OptimizerParamsBase {
  TransformOptimizerParams() { maxResults = 20; }
  // Line padding for internal NavOptimizer calls (visual delete path).
  // Lower default than CompositionOptimizer since effectiveLines already
  // includes prefix/suffix context. Adjustable to 0 for no padding.
  // See dev/optimizer/buffer-slicing.md for details.
  int navLinePaddingAbove = 1;
  int navLinePaddingBelow = 1;

  // Maximum number of results to keep per starting cursor position.
  int maxMultiplePerStartPosition = 1;

  // Chainable setters for fluent configuration
  TransformOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  TransformOptimizerParams& withMaxNodesPopped(int v) { maxNodesPopped = v; return *this; }
  TransformOptimizerParams& withNavLinePaddingAbove(int v) { navLinePaddingAbove = v; return *this; }
  TransformOptimizerParams& withNavLinePaddingBelow(int v) { navLinePaddingBelow = v; return *this; }
  TransformOptimizerParams& withNavLinePadding(int v) { navLinePaddingAbove = navLinePaddingBelow = v; return *this; }
  TransformOptimizerParams& withMaxMultiplePerStartPosition(int v) { maxMultiplePerStartPosition = v; return *this; }
  TransformOptimizerParams& withMinCountRepeat(int v) { setMinCountRepeat(v); return *this; }
  TransformOptimizerParams& withMaxCountRepeat(int v) { setMaxCountRepeat(v); return *this; }
  // Factory for Dijkstra mode (no heuristic)
  static TransformOptimizerParams dijkstra(int maxResults = 10, int maxNodesPopped = 50000) {
    TransformOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesPopped = maxNodesPopped;
    p.distanceWeight = 0.0;
    return p;
  }
};
