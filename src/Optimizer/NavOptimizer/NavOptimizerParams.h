#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// Field list for NavOptimizer. See OptimizerParamsBase.h for the X-macro
// contract.

#define NAV_OWN_FIELDS(F)                                                               \
    F(int, maxResultsPerEndPos, withMaxResultsPerEndPos, 1, parseInt)

#define NAV_FIELDS(F)        \
    MOTION_CLASS_FIELDS(F)   \
    NAV_OWN_FIELDS(F)

struct NavOptimizerParams : OptimizerParamsBase {
  NAV_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)

#define VF_PARAMS_SELF NavOptimizerParams
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_WITH_SETTER)
  NAV_FIELDS(VF_PARAMS_WITH_SETTER)
#undef VF_PARAMS_SELF

  // C++20 designated init can't reach base-class fields, so chained setters.
  static NavOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    return NavOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxNodesPopped)
        .withDistanceWeight(0.0);
  }
};
