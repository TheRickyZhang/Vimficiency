#pragma once

#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/FrontierCommon.h"

struct TransformFrontierQuery : FrontierQuery {
  const DiffState& diff;
};

std::vector<Suggestion> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config);
