#pragma once

#include <optional>

// Shared search parameters across all optimizers.
// Can be set as defaults in constructor and optionally overridden per-call.
struct OptimizerParams {
  int maxResults = 5;
  int maxSearchDepth = 100000;
  double costWeight = 1.0;
  double exploreFactor = 2.0;
  int fMotionThreshold = 2;

  OptimizerParams() = default;

  // Convenience constructor for common case of just setting maxResults
  explicit OptimizerParams(int maxResults)
      : maxResults(maxResults) {}

  OptimizerParams(int maxResults, int maxSearchDepth,
                  double costWeight = 1.0, double exploreFactor = 2.0,
                  int fMotionThreshold = 2)
      : maxResults(maxResults),
        maxSearchDepth(maxSearchDepth),
        costWeight(costWeight),
        exploreFactor(exploreFactor),
        fMotionThreshold(fMotionThreshold) {}

  // Merge: override only the fields that are explicitly set in 'override'
  // For simple structs, we just use the override directly if provided
  static OptimizerParams merge(const OptimizerParams& defaults,
                                const std::optional<OptimizerParams>& override) {
    return override.value_or(defaults);
  }
};
