#pragma once

#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/FrontierCommon.h"

struct TransformFrontierQuery : FrontierQuery {
  const DiffState& diff;
  // Cap on results retained per starting cursor position. Default 1
  // keeps only the cheapest sequence per start. Currently unused by the
  // depth-1 frontier (TransformExplorer enumerates structural atoms
  // directly and dedup is unnecessary because each enumerator produces
  // its own command-shape lane), but kept for API symmetry with
  // NavFrontierQuery::maxResultsPerEndPos and future cap-at-N work.
  int maxResultsPerStartPos = 1;
};

std::vector<Suggestion> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config);
