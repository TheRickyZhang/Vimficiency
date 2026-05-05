#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// Field list for TransformOptimizer. See OptimizerParamsBase.h for the
// X-macro contract.

#define TRANSFORM_FIELDS(F)                                                             \
    F(int, maxResultsPerStartPos, withMaxResultsPerStartPos, 1, parseInt)

struct TransformOptimizerParams : OptimizerParamsBase {
  TRANSFORM_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)

#define VF_PARAMS_SELF TransformOptimizerParams
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_WITH_SETTER)
  TRANSFORM_FIELDS(VF_PARAMS_WITH_SETTER)
#undef VF_PARAMS_SELF

  static TransformOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    return TransformOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxNodesPopped)
        .withDistanceWeight(0.0);
  }
};
