#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// NavOptimizerParams - Parameters for NavOptimizer::optimize()
// =============================================================================

struct NavOptimizerParams : OptimizerParamsBase {
  // Explicit default: matches base but documents intent for single-goal search.
  NavOptimizerParams() { maxResults = 10; }

  // Minimum column distance before exploring f-motion (f{char}). Recommended 2-5.
  int fMotionThreshold = 3;

  // Direction-based motion pruning (6-class model):
  // When true, explores only 3-4 of 6 motion classes based on relative position to goal.
  // Trade-off: faster exploration but may miss some edge-case optimal paths.
  bool useDirectionalPruning = true;

  // Line padding for sub-buffer slicing.
  // Controls how many lines above/below the search region to include.
  // Allows overshoot-and-return paths while bounding search space.
  // See dev/optimizer/buffer-slicing.md for details.
  int linePaddingAbove = 2;
  int linePaddingBelow = 2;

  // Cap on results retained per landing (end) position. Default 1 keeps
  // only the cheapest path per landing cell. Values >1 keep multiple
  // distinct paths reaching the same cell — useful for surfacing
  // alternatives like `w` / `W` / `e` that all land on the same word
  // start. Currently the search treats anything > 1 as "all" (no cap)
  // because the data structure stores one-per-cell or all-per-cell;
  // true cap-at-N may be added later.
  int maxResultsPerEndPos = 1;

  // Chainable setters for fluent configuration
  NavOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  NavOptimizerParams& withMaxNodesPopped(int v) { maxNodesPopped = v; return *this; }
  NavOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  NavOptimizerParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  NavOptimizerParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  NavOptimizerParams& withLinePaddingAbove(int v) { linePaddingAbove = v; return *this; }
  NavOptimizerParams& withLinePaddingBelow(int v) { linePaddingBelow = v; return *this; }
  NavOptimizerParams& withLinePadding(int v) { linePaddingAbove = linePaddingBelow = v; return *this; }
  NavOptimizerParams& withMinCountRepeat(int v) { setMinCountRepeat(v); return *this; }
  NavOptimizerParams& withMaxCountRepeat(int v) { setMaxCountRepeat(v); return *this; }
  NavOptimizerParams& withMaxResultsPerEndPos(int v) { maxResultsPerEndPos = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static NavOptimizerParams dijkstra(int maxResults = 10, int maxNodesPopped = 50000) {
    NavOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesPopped = maxNodesPopped;
    p.distanceWeight = 0.0;
    return p;
  }
};
