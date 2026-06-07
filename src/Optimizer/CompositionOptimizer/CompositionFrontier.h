#pragma once

// =============================================================================
// Composition frontier — single direct edit-bearing tokens
// =============================================================================
// Parallel to rankNavFrontier. Where NavFrontier surfaces motions toward
// edit sites, CompositionFrontier surfaces single Vim primitives whose
// activation regions go beyond ordinary TransformFrontier start columns:
//
//   - Pure-insertion shortcuts: `i / a / I / A / o`. Activation:
//     cursor inside the strategy's insertion column range.
//   - Bracket/quote text objects: `di" / da{ / ci( / ca[ / ...`. Activation:
//     cursor at the enumerated `(line, col)` of a visible bracket/quote.
//   - Join shortcut: bare `J`. Activation: cursor on the join plan's entry
//     line AND one `J` makes progress on the planned diff (without
//     reaching the fencepost — that case is owned by TransformFrontier's
//     explorer J lane).
//
// Invariant: every emitted Suggestion's `token` is a single immediately
// executable Vim action. No motion prefix. No typed insert payload. No
// `<Esc>`. Movement to a composition activation region is NavFrontier's
// job. Typed payload + `<Esc>` after Insert-entering actions is owned by
// Explore's Insert phase, derived from observed state.
//
// Tokens across all returned suggestions are pairwise distinct
// (release-active CHECK).

#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/FrontierCommon.h"
#include "Types/NavContext.h"

struct CompositionFrontierQuery : FrontierQuery {
  const DiffState& diff;
  const NavBoundary& boundary;
  const NavContext& navContext;
};

// Depth-1 enumeration of composition actions for `query.diff`. Returns up
// to `query.maxCount` Suggestions sorted by cost (cheapest first). Empty
// when the diff has no composition shortcuts available from this cursor.
std::vector<Suggestion> rankCompositionFrontier(
    const CompositionFrontierQuery& query,
    const Config& config);
