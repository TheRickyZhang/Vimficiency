#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// EditOptimizerParams - Parameters for EditOptimizer
// =============================================================================
// EditOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total results across all positions.

struct EditOptimizerParams : OptimizerParamsBase {
  // Currently no edit-specific parameters beyond the base.
  // This struct exists for future extensibility and type distinction.

  // Chainable setters for fluent configuration
  EditOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  EditOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  EditOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }
  EditOptimizerParams& withTrackExploredStates(bool v) { trackExploredStates = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static EditOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    EditOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesExplored = maxNodesExplored;
    p.distanceWeight = 0.0;
    return p;
  }
};
