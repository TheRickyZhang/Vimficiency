#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// CompositionOptimizerParams - Parameters for CompositionOptimizer::optimize()
// =============================================================================

struct CompositionOptimizerParams : OptimizerParamsBase {
  // Explicit default: matches base but documents intent.
  CompositionOptimizerParams() { maxResults = 10; }

  // Minimum column distance before exploring f-motion (f{char}). Recommended 2-5.
  int fMotionThreshold = 3;

  // Direction-based motion pruning (6-class model):
  // When true, explores only 3-4 of 6 motion classes based on relative position to goal.
  // Trade-off: faster exploration but may miss some edge-case optimal paths.
  bool useDirectionalPruning = true;

  // Line padding for MotionOptimizer calls.
  // Controls how many lines above/below the search region to include.
  // Allows overshoot-and-return paths while bounding search space.
  // See dev/optimizer/buffer-slicing.md for details.
  int motionPaddingAbove = 1;
  int motionPaddingBelow = 1;

  // Heuristic penalty for overshooting (going past the next edit region).
  // Overshooting is penalized more than undershooting since it requires backtracking.
  double overshootPenalty = 3.0;

  // Maximum number of edit alternatives per starting position to store and explore.
  // Higher values let CompositionOptimizer consider suboptimal edits that may compose
  // better with surrounding motion context.
  int maxEditResultsPerPosition = 1;

  // Chainable setters for fluent configuration
  CompositionOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  CompositionOptimizerParams& withMaxNodesPopped(int v) { maxNodesPopped = v; return *this; }
  CompositionOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  CompositionOptimizerParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  CompositionOptimizerParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  CompositionOptimizerParams& withMotionLinePaddingAbove(int v) { motionPaddingAbove = v; return *this; }
  CompositionOptimizerParams& withMotionLinePaddingBelow(int v) { motionPaddingBelow = v; return *this; }
  CompositionOptimizerParams& withMotionLinePadding(int v) { motionPaddingAbove = motionPaddingBelow = v; return *this; }
  CompositionOptimizerParams& withMinCountRepeat(int v) { setMinCountRepeat(v); return *this; }
  CompositionOptimizerParams& withMaxCountRepeat(int v) { setMaxCountRepeat(v); return *this; }
  CompositionOptimizerParams& withMaxEditResultsPerPosition(int v) { maxEditResultsPerPosition = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static CompositionOptimizerParams dijkstra(int maxResults = 10, int maxNodesPopped = 50000) {
    CompositionOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesPopped = maxNodesPopped;
    p.distanceWeight = 0.0;
    return p;
  }
};

// =============================================================================
// CompositionOptimizerRangeParams - Extended params with range-specific options
// =============================================================================
// Extends CompositionOptimizerParams with range-specific options.

struct CompositionOptimizerRangeParams : CompositionOptimizerParams {
  // When false (default): at most 1 result per unique end position (best cost)
  // When true: allows multiple results per position (all found paths)
  // Note: maxResults limits total results, not unique positions
  bool allowMultiplePerPosition = false;

  // Chainable setters (return derived type for chaining)
  CompositionOptimizerRangeParams& withMaxResults(int v) { maxResults = v; return *this; }
  CompositionOptimizerRangeParams& withMaxNodesPopped(int v) { maxNodesPopped = v; return *this; }
  CompositionOptimizerRangeParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  CompositionOptimizerRangeParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  CompositionOptimizerRangeParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  CompositionOptimizerRangeParams& withAllowMultiplePerPosition(bool v) { allowMultiplePerPosition = v; return *this; }
  CompositionOptimizerRangeParams& withMotionLinePaddingAbove(int v) { motionPaddingAbove = v; return *this; }
  CompositionOptimizerRangeParams& withMotionLinePaddingBelow(int v) { motionPaddingBelow = v; return *this; }
  CompositionOptimizerRangeParams& withMotionLinePadding(int v) { motionPaddingAbove = motionPaddingBelow = v; return *this; }
  CompositionOptimizerRangeParams& withMinCountRepeat(int v) { setMinCountRepeat(v); return *this; }
  CompositionOptimizerRangeParams& withMaxCountRepeat(int v) { setMaxCountRepeat(v); return *this; }
};
