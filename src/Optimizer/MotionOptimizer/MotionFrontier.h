#pragma once

// =============================================================================
// Motion frontier — immediate next-molecule ranking
// =============================================================================
// Depth-1 live peek of the motion A* search graph from the cursor, scoped
// to what `:Vimfy explore` needs: show the user the top-K SINGLE atomic
// motions they could take right now toward the target, not the top-K full
// paths to it.
//
// How it relates to MotionOptimizer:
//   - Same `MotionExplorer` and candidate-emission callbacks as
//     `MotionOptimizer::optimizeToRange`. Same scoring function
//     (`effortWeight * effort + distanceWeight * distanceToRange`).
//   - But only ONE expansion level: we build a single `MotionState` at the
//     cursor, emit all depth-1 successors, sort by A* cost, and return the
//     top K. No priority queue, no iterative popping, no path stitching.
//   - The full optimizer's cost-map filters no-progress states on re-entry
//     (you can't re-visit the cursor cell at higher cost). Depth-1 has no
//     prior entry to compare against, so no-op motions (landing == cursor)
//     must be filtered explicitly at emission time — see the `isNoOp`
//     guard in rankMotionFrontier.
//
// Each returned item's `cost` / `landingPos` describe that single molecule
// in isolation — NOT a full path. The caller (Explore::Session) uses these
// to drive a step-by-step UI; after the user picks a molecule, they call
// `rankMotionFrontier` again from the new cursor for the next step.
//
// No state is cached between calls. Each invocation rebuilds the
// `BufferIndex` + `MotionExplorer` from the live inputs; this is cheap for
// depth 1 and sidesteps the undo/redo complexity of a persistent
// optimizer.

#include <string>
#include <vector>

#include "Boundary/MotionBoundary.h"
#include "Keyboard/Config.h"
#include "Types/CharRange.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

struct MotionFrontierItem {
  std::string molecule;
  CursorPos landingPos{0, 0};
  // Raw effort of this single molecule (not a cumulative path cost).
  double cost = 0.0;
};

struct MotionFrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  CharRange targetRange;
  const MotionBoundary& boundary;
  const NavContext& navContext;
  int maxCount = 0;
  // When false (default): at most one result per unique landing position —
  // the cheapest-cost molecule wins, matching the semantic of
  // `MotionOptimizerRangeParams::allowMultiplePerPosition`.
  // When true: multiple molecules reaching the same cell are kept (e.g. `w`
  // and `W` both landing on the next word start); dedup keys on sequence
  // text so only literal-string duplicates collapse.
  bool allowMultiplePerPosition = false;
};

// Depth-1 live expansion from `query.cursor` toward `query.targetRange`.
// Returns up to `query.maxCount` distinct single-molecule candidates,
// sorted by A* priority (lowest cost first). Empty if the cursor is
// already on-target or no motion makes progress.
std::vector<MotionFrontierItem> rankMotionFrontier(
    const MotionFrontierQuery& query,
    const Config& config);

// Convenience wrapper: target any cell on `targetLine`. Used by the
// join-plan hint path in Explore for multiline diffs that will collapse
// via `J`. Internally funnels through `rankMotionFrontier` with a line-
// wide target range.
std::vector<MotionFrontierItem> rankMotionFrontierToLine(
    const Lines& lines,
    CursorPos cursor,
    int targetLine,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const Config& config,
    int maxCount);
