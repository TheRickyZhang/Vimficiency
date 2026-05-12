#pragma once

// =============================================================================
// Post-explorer emissions — single source of truth
// =============================================================================
// Structurals that supplement the per-step enumeration in TransformExplorer.
// Both the full optimizer's finalize paths and the depth-1 frontier call
// these helpers directly, so eligibility and sequence construction stay
// aligned.

#include <optional>
#include <string_view>

#include "Boundary/TransformBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/Result.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Types/Lines.h"

namespace TransformPostExplorer {

// Emits `r{char}` / `Nr{char}` for single-line same-length replacement diffs.
std::optional<Result> tryReplacement(
    std::string_view deleted,
    std::string_view inserted,
    const Config& config,
    double maxEffort);

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
