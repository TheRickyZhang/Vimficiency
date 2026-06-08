#pragma once

#include "Optimizer/OptimizerParamsBase.h"

// Field list for CompositionOptimizer. See OptimizerParamsBase.h for the
// X-macro contract.

#define COMPOSITION_OWN_FIELDS(F)                                                          \
    F(int,    navPaddingAbove, withNavLinePaddingAbove, 1, parseInt)                       \
    F(int,    navPaddingBelow, withNavLinePaddingBelow, 1, parseInt)                       \
    F(double, overshootPenalty, withOvershootPenalty, 3.0, parseDouble)                    \
    F(double, diffOpenPenalty, withDiffOpenPenalty, 1.0, parseDouble)                      \
    F(double, moveDeleteScale, withMoveDeleteScale, 1.0, parseDouble)                      \
    F(int,    transformMaxResultsPerStartPos, withTransformMaxResultsPerStartPos, 1, parseInt) \
    F(int,    diffAlgorithm, withDiffAlgorithm, 0, parseInt)  /* 0 = VimDiff default; 1 = MyersDiff fallback */

#define COMPOSITION_FIELDS(F)     \
    MOTION_CLASS_FIELDS(F)        \
    COMPOSITION_OWN_FIELDS(F)

struct CompositionOptimizerParams : OptimizerParamsBase {
  COMPOSITION_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)

#define VF_PARAMS_SELF CompositionOptimizerParams
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_WITH_SETTER)
  COMPOSITION_FIELDS(VF_PARAMS_WITH_SETTER)
#undef VF_PARAMS_SELF

  CompositionOptimizerParams& withNavLinePadding(int v) {
    navPaddingAbove = navPaddingBelow = v;
    return *this;
  }

  static CompositionOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    return CompositionOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxNodesPopped)
        .withDistanceWeight(0.0);
  }
};
