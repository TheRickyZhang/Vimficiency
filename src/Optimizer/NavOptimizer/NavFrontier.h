#pragma once

// =============================================================================
// Motion frontier — immediate next-token ranking
// =============================================================================
// Depth-1 live peek of the motion A* search graph from the cursor, scoped
// to what `:Vimfy explore` needs: show the user the top-K SINGLE atomic
// motion tokens they could take right now toward the target, not the top-K full
// paths to it.
//
// How it relates to NavOptimizer:
//   - Same `NavExplorer` and candidate-emission callbacks as
//     `NavOptimizer::optimize`. Same scoring function
//     (`effortWeight * effort + distanceWeight * distanceToRange`).
//   - But only ONE expansion level: we build a single `NavState` at the
//     cursor, emit all depth-1 successors, sort by A* cost, and return the
//     top K. No priority queue, no iterative popping, no path stitching.
//   - The full optimizer's cost-map filters no-progress states on re-entry
//     (you can't re-visit the cursor cell at higher cost). Depth-1 has no
//     prior entry to compare against, so no-op motions (landing == cursor)
//     must be filtered explicitly at emission time — see the `isNoOp`
//     guard in rankNavFrontier.
//
// Each returned item's `costDiff` / `landingPos` describe the next accepted
// token. `costDiff` includes keyboard-model boundary effects from
// `query.seq`. The caller (Explore::View) uses these to drive a
// step-by-step UI; after the user picks a token, they call `rankNavFrontier`
// again from the new cursor for the next step.
//
// No state is cached between calls. Each invocation rebuilds the
// `BufferIndex` + `NavExplorer` from the live inputs; this is cheap for
// depth 1 and sidesteps the undo/redo complexity of a persistent
// optimizer.

#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/FrontierCommon.h"
#include "Types/CharRange.h"
#include "Types/NavContext.h"


struct NavFrontierQuery : FrontierQuery {
  CharRange targetRange;
  const NavBoundary& boundary;
  const NavContext& navContext;
  // Cap on results retained per landing (end) cell. Default 1 keeps only
  // the cheapest token per cell. Values > 1 surface multiple distinct
  // tokens reaching the same cell (e.g., `w`/`W`/`e` all landing on the
  // same word start). The frontier currently treats anything > 1 as "all"
  // (no cap); true cap-at-N is a future refinement (Phase B.4).
  int maxResultsPerEndPos = 1;
};

// Depth-1 live expansion from `query.cursor` toward `query.targetRange`.
// Returns up to `query.maxCount` distinct single-token candidates,
// sorted by A* priority (lowest cost first). Empty if the cursor is
// already on-target or no motion makes progress.
std::vector<Suggestion> rankNavFrontier(
    const NavFrontierQuery& query,
    const Config& config);
