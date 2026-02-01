#pragma once

// =============================================================================
// MotionOptimizerParams - Parameters for MotionOptimizer::optimize()
// =============================================================================

struct MotionOptimizerParams {
  // Maximum results (paths) to find to the goal position
  int maxResults = 10;

  // Maximum states processed from priority queue
  int maxNodesExplored = 50000;

  // Search stops when effort > userEffort * exploreFactor
  double exploreFactor = 2.0;

  // Minimum column distance before exploring f-motion (f{char}). Recommended 2-5.
  int fMotionThreshold = 3;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // (default A*): effortWeight=1.0, distanceWeight=1.0
  // (Dijkstra):   effortWeight=1.0, distanceWeight=0.0
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  // Direction-based motion pruning (6-class model):
  // When true, explores only 3-4 of 6 motion classes based on relative position to goal.
  // Trade-off: faster exploration but may miss some edge-case optimal paths.
  bool useDirectionalPruning = true;

  // Debug: collect explored states in SearchStats (expensive, for debugging only)
  bool trackExploredStates = false;

  // Chainable setters for fluent configuration
  MotionOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  MotionOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  MotionOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  MotionOptimizerParams& withFMotionThreshold(int v) { fMotionThreshold = v; return *this; }
  MotionOptimizerParams& withDirectionalPruning(bool v) { useDirectionalPruning = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static MotionOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    return MotionOptimizerParams{
        .maxResults = maxResults,
        .maxNodesExplored = maxNodesExplored,
        .distanceWeight = 0.0,
    };
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
  MotionOptimizerRangeParams& withAllowMultiplePerPosition(bool v) { allowMultiplePerPosition = v; return *this; }
};
