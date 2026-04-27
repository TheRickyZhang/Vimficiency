#pragma once

// =============================================================================
// Motion frontier — immediate next-molecule ranking
// =============================================================================
// Depth-1 live peek of the motion A* search graph from the cursor, scoped
// to what `:Vimfy explore` needs: show the user the top-K SINGLE atomic
// motions they could take right now toward the target, not the top-K full
// paths to it.
//
// How it relates to NavOptimizer:
//   - Same `NavExplorer` and candidate-emission callbacks as
//     `NavOptimizer::optimizeToRange`. Same scoring function
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
// Each returned item's `cost` / `landingPos` describe that single molecule
// in isolation — NOT a full path. The caller (Explore::View) uses these
// to drive a step-by-step UI; after the user picks a molecule, they call
// `rankNavFrontier` again from the new cursor for the next step.
//
// No state is cached between calls. Each invocation rebuilds the
// `BufferIndex` + `NavExplorer` from the live inputs; this is cheap for
// depth 1 and sidesteps the undo/redo complexity of a persistent
// optimizer.

#include <optional>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/FrontierCommon.h"
#include "Types/CharRange.h"
#include "Types/NavContext.h"

// Default dedup (`allowMultiplePerPosition == false`): at most one result
// per unique landing position — the cheapest-cost molecule wins, matching
// `NavOptimizerRangeParams::allowMultiplePerPosition`. When true: multiple
// molecules reaching the same cell are kept (e.g. `w` and `W` both landing
// on the next word start); dedup keys on sequence text so only literal-
// string duplicates collapse.
struct NavFrontierItem : FrontierItem {
  // Used only by Explore::backfillEditStartMovements: cost of the planned
  // edit at `goalPos`. The wire's totalPathCost = cost + this.value_or(0).
  // Empty for every other nav source.
  std::optional<double> projectedEditCost;
};

struct NavFrontierQuery : FrontierQuery {
  CharRange targetRange;
  const NavBoundary& boundary;
  const NavContext& navContext;
};

// Depth-1 live expansion from `query.cursor` toward `query.targetRange`.
// Returns up to `query.maxCount` distinct single-molecule candidates,
// sorted by A* priority (lowest cost first). Empty if the cursor is
// already on-target or no motion makes progress.
std::vector<NavFrontierItem> rankNavFrontier(
    const NavFrontierQuery& query,
    const Config& config);

// Convenience wrapper: target any cell on `targetLine`. Used by the
// join-plan hint path in Explore for multiline diffs that will collapse
// via `J`. Internally funnels through `rankNavFrontier` with a line-
// wide target range.
std::vector<NavFrontierItem> rankNavFrontierToLine(
    const Lines& lines,
    CursorPos cursor,
    int targetLine,
    const NavBoundary& boundary,
    const NavContext& navContext,
    const Config& config,
    int maxCount);
