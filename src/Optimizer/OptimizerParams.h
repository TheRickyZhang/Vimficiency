#pragma once

// Shared search parameters across all optimizers.
// Use designated initializers to override defaults
struct OptimizerParams {
  int maxResults = 10;
  int maxNodesExplored = 50000;
  double exploreFactor = 2.0;
  int fMotionThreshold = 2;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // (default): effortWeight=1.0, distanceWeight=1.0
  // Dijkstra:  effortWeight=1.0, distanceWeight=0.0
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  // Direction-based motion pruning (6-class model):
  // When true, explores only 2-4 of 6 motion classes based on relative position to goal.
  // Trade-off: faster exploration but may miss some edge-case optimal paths.
  // When false, explores all motion classes (original behavior).
  // Default: false (benchmarks show some edge cases regress with pruning)
  bool useDirectionalPruning = false;

  // No distance weight = dijkstra instead of A*.
  static OptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    return OptimizerParams{
        .maxResults = maxResults,
        .maxNodesExplored = maxNodesExplored,
        .distanceWeight = 0.0,
    };
  }
};
