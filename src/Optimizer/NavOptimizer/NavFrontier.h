#pragma once


#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/FrontierCommon.h"
#include "Types/CharRange.h"
#include "Types/NavContext.h"


struct NavFrontierQuery : FrontierQuery {
  // Must be non-empty. Callers expand zero-width (pure-insertion) targets
  // upstream — see Explore::recommendNavigate.
  CharRange targetRange;
  const NavBoundary& boundary;
  const NavContext& navContext;
};

// Depth-1 live expansion from `query.cursor` toward `query.targetRange`.
// Returns up to `query.maxCount` distinct single-token candidates,
// sorted by A* priority (lowest cost first). Empty if the cursor is
// already on-target or no motion makes progress.
std::vector<Suggestion> rankNavFrontier(
    const NavFrontierQuery& query,
    const Config& config);
