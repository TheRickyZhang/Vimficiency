#pragma once

// =============================================================================
// Composition motion frontier — depth-1 cross-phase shortcuts
// =============================================================================
// Parallel to rankNavFrontier. Where NavFrontier surfaces pure motions toward
// the diff range, CompositionFrontier surfaces *composition motions*:
// sequences that telescope motion + transform progress into one stroke,
// mirroring the optimizer's CompositionOptimizer emissions that fall outside
// the strict Navigate→Transform alternation.
//
// Three families correspond 1:1 with CompositionOptimizer.cpp's
// cross-phase emission sites:
//   - Pure-insertion shortcuts:  I/A/o (line-scope), i/a (point-scope)
//                                + typed insert payload + <Esc>
//                                (mirrors `exploreInsertionStrategy`)
//   - Bracket/quote shortcuts:   di"/da{/ci"/ca{ + (typed for replacement)
//                                fired from any column where the bracket
//                                pair is visible (`validQuoteMask` /
//                                `validBracketMask`)
//                                (mirrors the bqContext block)
//   - joinPlan shortcut:          J/gJ/NJ/NgJ + optional embedded edit,
//                                fired from any column on the entry line
//                                (mirrors the joinPlan dispatch)
//
// Each emitted Suggestion's `token` is the FULL composition sequence
// (motion prefix + structural [+ typed]) — what the user actually types.
// Cost is computed end-to-end via RunningEffort to match the optimizer's
// scoring; `landingPos` is the cursor position after the structural
// completes (i.e. ready for Insert phase or already past the edit).
//
// Caller (Explore::recommendNavigate) is responsible for merging the
// CompositionFrontier output with rankNavFrontier output and ranking the
// combined set by cost.

#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/FrontierCommon.h"
#include "Types/NavContext.h"

struct CompositionFrontierQuery : FrontierQuery {
  const DiffState& diff;
  const NavBoundary& boundary;
  const NavContext& navContext;
};

// Depth-1 enumeration of composition motions for `query.diff`. Returns up
// to `query.maxCount` Suggestions sorted by cost (cheapest first). Empty
// when the diff has no composition shortcuts available from this cursor.
std::vector<Suggestion> rankCompositionFrontier(
    const CompositionFrontierQuery& query,
    const Config& config);
