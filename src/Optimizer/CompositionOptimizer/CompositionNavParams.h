#pragma once

#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"

inline NavOptimizerParams navParamsForCompositionMotion(
    const CompositionOptimizerParams& params) {
  return NavOptimizerParams{}
      .withMinCountRepeat(params.minPrefixCount)
      .withMaxCountRepeat(params.maxPrefixCount)
      .withFMotionThreshold(params.fMotionThreshold)
      .withDirectionalPruning(params.useDirectionalPruning);
}
