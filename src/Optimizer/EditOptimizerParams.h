#pragma once

// =============================================================================
// EditOptimizerParams - Parameters for EditOptimizer
// =============================================================================
// EditOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total results across all positions.
// exploreFactor stops search when cost > exploreFactor * minCost.

struct EditOptimizerParams {
  // Maximum total results found across all starting positions
  int maxResults = 10;

  // Maximum nodes to explore before stopping search
  int maxNodesExplored = 50000;

  // Search stops when effort > minEffort * exploreFactor
  // minEffort = min(user sequence effort if provided, cheapest result found)
  double exploreFactor = 2.0;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // (default A*): effortWeight=1.0, distanceWeight=1.0
  // (Dijkstra):   effortWeight=1.0, distanceWeight=0.0
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  // Debug: collect explored states in SearchStats (expensive, for debugging only)
  bool trackExploredStates = false;

  // Chainable setters for fluent configuration
  EditOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  EditOptimizerParams& withMaxNodesExplored(int v) { maxNodesExplored = v; return *this; }
  EditOptimizerParams& withExploreFactor(double v) { exploreFactor = v; return *this; }

  // Factory for Dijkstra mode (no heuristic)
  static EditOptimizerParams dijkstra(int maxResults = 10, int maxNodesExplored = 50000) {
    return EditOptimizerParams{
        .maxResults = maxResults,
        .maxNodesExplored = maxNodesExplored,
        .distanceWeight = 0.0,
    };
  }
};
