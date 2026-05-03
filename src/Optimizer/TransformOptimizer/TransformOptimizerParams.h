#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// =============================================================================
// TransformOptimizerParams - Parameters for TransformOptimizer
// =============================================================================
// TransformOptimizer searches from ALL starting positions simultaneously.
// maxResults limits total completions reaching goal state.
// Higher default than other optimizers because multi-source search produces
// duplicate completions (same position reached via different paths).
// The search also stops when all starts become terminal
// (either capped or exhausted) in shouldContinue().

struct TransformOptimizerParams : OptimizerParamsBase {

  // Cap on results retained per starting cursor position. Default 1 keeps
  // only the cheapest sequence per start; values >1 surface multiple
  // distinct command shapes from the same start (pedagogically useful for
  // showing alternatives). Search becomes terminal for a start once its
  // bucket fills.
  int maxResultsPerStartPos = 1;

  // Chainable setters for fluent configuration
  TransformOptimizerParams& withMaxResults(int v) { maxResults = v; return *this; }
  TransformOptimizerParams& withMaxNodesPopped(int v) { maxNodesPopped = v; return *this; }
  TransformOptimizerParams& withMaxResultsPerStartPos(int v) { maxResultsPerStartPos = v; return *this; }
  TransformOptimizerParams& withMinCountRepeat(int v) { setMinCountRepeat(v); return *this; }
  TransformOptimizerParams& withMaxCountRepeat(int v) { setMaxCountRepeat(v); return *this; }
  // Factory for Dijkstra mode (no heuristic)
  static TransformOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    TransformOptimizerParams p;
    p.maxResults = maxResults;
    p.maxNodesPopped = maxNodesPopped;
    p.distanceWeight = 0.0;
    return p;
  }
};
