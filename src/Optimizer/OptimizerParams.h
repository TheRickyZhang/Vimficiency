#pragma once

#include <optional>

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

  // No distance weight = dijkstra instead of A*.
  static OptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    return OptimizerParams{
        .maxResults = maxResults,
        .maxNodesExplored = maxNodesExplored,
        .distanceWeight = 0.0,
    };
  }

  // Merge: override only the fields that are explicitly set in 'override'
  static OptimizerParams merge(const OptimizerParams& defaults,
                                const std::optional<OptimizerParams>& override) {
    return override.value_or(defaults);
  }
};
