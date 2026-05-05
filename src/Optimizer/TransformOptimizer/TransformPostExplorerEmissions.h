#pragma once

// =============================================================================
// Post-explorer emissions — single source of truth
// =============================================================================
// Structurals that supplement the per-step enumeration in TransformExplorer.
// Both consumers (the full optimizer's `*GoalHandler::finalize` and the
// depth-1 `enumerateDepth1DeletionStructurals`) must apply these uniformly.
//
// Adding a new emission here means updating:
//   - Whichever GoalHandler currently emits it in finalize, AND
//   - The depth-1 frontier in TransformFrontier.cpp.
// If you only update one consumer, you've created a parity gap.
//
// Current entries:
//   - tryReplacement     (r{char} / Nr{char}) — single-line same-length
//                        replacement diffs. Lives on ChangeGoalHandler;
//                        re-declared here only for visibility.
//   - tryVisualDelete    (v{motion}d) — full-region pure-deletion diffs.

#include <optional>

#include "Boundary/TransformBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/Result.h"
#include "Optimizer/TransformOptimizer/ChangeGoalHandler.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Types/Lines.h"

namespace TransformPostExplorer {

// Builds a `v{motion}d` candidate that selects the entire deletion region
// and deletes it. Returns nullopt when there's no spannable content (single
// position or boundary collapse). Used only for pure-deletion diffs — the
// optimizer never emits a visual variant for replacement diffs.
//
// `effectiveLines` is the prefix+content+suffix view (matches what the
// optimizer's main loop sees and what `enumerateDepth1DeletionStructurals`
// constructs).
std::optional<Result> tryVisualDelete(
    const Lines& effectiveLines,
    int leftColOffset,
    int rightColOffset,
    const TransformBoundary& transformBoundary,
    const TransformOptimizerParams& params,
    const Config& config);

}  // namespace TransformPostExplorer
