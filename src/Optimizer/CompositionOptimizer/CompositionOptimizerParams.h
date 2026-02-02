#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// CompositionOptimizerParams - Parameters for CompositionOptimizer::optimize()
// =============================================================================

struct CompositionOptimizerParams : OptimizerParamsBase {
  // Minimum column distance before exploring f-motion (f{char}). Recommended 2-5.
  int fMotionThreshold = 3;

  // Direction-based motion pruning (6-class model):
  // When true, explores only 3-4 of 6 motion classes based on relative position to goal.
  // Trade-off: faster exploration but may miss some edge-case optimal paths.
  bool useDirectionalPruning = true;

  // Line subset padding for MotionOptimizer calls.
  // Controls how many lines before/after the search region to include.
  // Allows overshoot-and-return paths while bounding search space.
  int preSubbufferPadding = 3;
  int postSubbufferPadding = 3;

  // Chainable setters for fluent configuration
  CompositionOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  CompositionOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  CompositionOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  CompositionOptimizerParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  CompositionOptimizerParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  CompositionOptimizerParams& withTrackExploredStates(bool v) { trackExploredStates = v; return *this; }
  CompositionOptimizerParams& withPreSubbufferPadding(int v) { preSubbufferPadding = v; return *this; }
  CompositionOptimizerParams& withPostSubbufferPadding(int v) { postSubbufferPadding = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static CompositionOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    CompositionOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesExplored = maxNodesExplored;
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
  CompositionOptimizerRangeParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  CompositionOptimizerRangeParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  CompositionOptimizerRangeParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  CompositionOptimizerRangeParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }
  CompositionOptimizerRangeParams& withTrackExploredStates(bool v) { trackExploredStates = v; return *this; }
  CompositionOptimizerRangeParams& withAllowMultiplePerPosition(bool v) { allowMultiplePerPosition = v; return *this; }
  CompositionOptimizerRangeParams& withPreSubbufferPadding(int v) { preSubbufferPadding = v; return *this; }
  CompositionOptimizerRangeParams& withPostSubbufferPadding(int v) { postSubbufferPadding = v; return *this; }
};
