#pragma once

#include <optional>

// Shared search parameters across all optimizers.
// Can be set as defaults in constructor and optionally overridden per-call.
struct OptimizerParams {
  int maxResults = 5;
  int maxSearchDepth = 100000;
  double exploreFactor = 2.0;
  int fMotionThreshold = 2;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // A* (default):  effortWeight=1.0, distanceWeight=1.0
  // Dijkstra:      effortWeight=1.0, distanceWeight=0.0
  // Intermediate values allow tuning the balance between optimality and speed
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  OptimizerParams() = default;

  // Convenience constructor for common case of just setting maxResults
  explicit OptimizerParams(int maxResults)
      : maxResults(maxResults) {}

  OptimizerParams(int maxResults, int maxSearchDepth,
                  double exploreFactor = 2.0,
                  int fMotionThreshold = 2)
      : maxResults(maxResults),
        maxSearchDepth(maxSearchDepth),
        exploreFactor(exploreFactor),
        fMotionThreshold(fMotionThreshold) {}

  // Full constructor with A* weights
  OptimizerParams(int maxResults, int maxSearchDepth,
                  double exploreFactor,
                  int fMotionThreshold,
                  double effortWeight, double distanceWeight)
      : maxResults(maxResults),
        maxSearchDepth(maxSearchDepth),
        exploreFactor(exploreFactor),
        fMotionThreshold(fMotionThreshold),
        effortWeight(effortWeight),
        distanceWeight(distanceWeight) {}

  // Convenience: create Dijkstra params (no heuristic)
  static OptimizerParams dijkstra(int maxResults = 5, int maxSearchDepth = 100000) {
    OptimizerParams p(maxResults);
    p.maxSearchDepth = maxSearchDepth;
    p.distanceWeight = 0.0;
    return p;
  }

  // Merge: override only the fields that are explicitly set in 'override'
  // For simple structs, we just use the override directly if provided
  static OptimizerParams merge(const OptimizerParams& defaults,
                                const std::optional<OptimizerParams>& override) {
    return override.value_or(defaults);
  }
};
