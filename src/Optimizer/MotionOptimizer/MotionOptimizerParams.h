#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// MotionOptimizerParams - Parameters for MotionOptimizer::optimize()
// =============================================================================

struct MotionOptimizerParams : OptimizerParamsBase {
  // Explicit default: matches base but documents intent for single-goal search.
  MotionOptimizerParams() { maxResults = 10; }

  // Minimum column distance before exploring f-motion (f{char}). Recommended 2-5.
  int fMotionThreshold = 3;

  // Direction-based motion pruning (6-class model):
  // When true, explores only 3-4 of 6 motion classes based on relative position to goal.
  // Trade-off: faster exploration but may miss some edge-case optimal paths.
  bool useDirectionalPruning = true;

  // Line padding for sub-buffer slicing.
  // Controls how many lines above/below the search region to include.
  // Allows overshoot-and-return paths while bounding search space.
  // See docs/optimizer/buffer-slicing.md for details.
  int linePaddingAbove = 2;
  int linePaddingBelow = 2;

  // Chainable setters for fluent configuration
  MotionOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  MotionOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  MotionOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  MotionOptimizerParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  MotionOptimizerParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  MotionOptimizerParams& withTrackExploredStates(bool v) { trackExploredStates = v; return *this; }
  MotionOptimizerParams& withLinePaddingAbove(int v) { linePaddingAbove = v; return *this; }
  MotionOptimizerParams& withLinePaddingBelow(int v) { linePaddingBelow = v; return *this; }
  MotionOptimizerParams& withLinePadding(int v) { linePaddingAbove = linePaddingBelow = v; return *this; }
  MotionOptimizerParams& withMinCountRepeat(int v) { minCountRepeat = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static MotionOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    MotionOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesExplored = maxNodesExplored;
    p.distanceWeight = 0.0;
    return p;
  }
};

// =============================================================================
// MotionOptimizerRangeParams - Parameters for MotionOptimizer::optimizeToRange()
// =============================================================================
// Extends MotionOptimizerParams with range-specific options.

struct MotionOptimizerRangeParams : MotionOptimizerParams {
  // When false (default): at most 1 result per unique end position (best cost)
  // When true: allows multiple results per position (all found paths)
  // Note: maxResults limits total results, not unique positions
  bool allowMultiplePerPosition = false;

  // Chainable setters (return derived type for chaining)
  MotionOptimizerRangeParams& withMaxResults(int v) { maxResults = v; return *this; }
  MotionOptimizerRangeParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  MotionOptimizerRangeParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  MotionOptimizerRangeParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  MotionOptimizerRangeParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  MotionOptimizerRangeParams& withTrackExploredStates(bool v) { trackExploredStates = v; return *this; }
  MotionOptimizerRangeParams& withAllowMultiplePerPosition(bool v) { allowMultiplePerPosition = v; return *this; }
  MotionOptimizerRangeParams& withLinePaddingAbove(int v) { linePaddingAbove = v; return *this; }
  MotionOptimizerRangeParams& withLinePaddingBelow(int v) { linePaddingBelow = v; return *this; }
  MotionOptimizerRangeParams& withLinePadding(int v) { linePaddingAbove = linePaddingBelow = v; return *this; }
  MotionOptimizerRangeParams& withMinCountRepeat(int v) { minCountRepeat = v; return *this; }
};
