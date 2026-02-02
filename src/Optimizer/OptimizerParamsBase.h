#pragma once

// =============================================================================
// OptimizerParamsBase - Shared parameters for all optimizer types
// =============================================================================
// Base struct containing search parameters common to Motion, Edit, and
// Composition optimizers. Derived structs add optimizer-specific fields.

struct OptimizerParamsBase {
  // Maximum results (paths) to find
  int maxResults = 10;

  // Maximum non-stale states to process (actual work done, not queue pops).
  // Stale states (superseded by better paths) don't count toward this limit.
  // Internal safety cap at 10x this value prevents runaway loops from excessive stale nodes.
  int maxNodesExplored = 50000;

  // Search stops when effort > baseEffort * exploreFactor
  // baseEffort is typically user's sequence effort or cheapest result found
  double exploreFactor = 2.0;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // (default A*): effortWeight=1.0, distanceWeight=1.0
  // (Dijkstra):   effortWeight=1.0, distanceWeight=0.0
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  // Debug: collect explored states in SearchStats (expensive, for debugging only)
  bool trackExploredStates = false;
};
