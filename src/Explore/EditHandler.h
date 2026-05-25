#pragma once

#include <expected>
#include <string_view>

#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"

// Transform-side logic for Explore::View. Historical Edit naming retained
// because this layer consumes TransformResult from the current codebase.
//
// MIRROR boundary: everything that reads TransformResult::resultsAt / goalPosAt is
// semantic-coupled to the composition/transform optimizer and must be reviewed
// together when those outputs change shape.
// validateBufferState encodes the explore-specific strict-revert policy:
// accept iff the buffer matches either the current or next fencepost.

namespace Explore::EditHandler {

// Buffer-state validation under the strict (b) policy.
//   advance == true : newLines matches nextFencepost (advance phase).
//   advance == false: newLines matches currentFencepost
//                     (no-op — native undo or re-sync).
// Reject (Rejected) : drift outside the planned edit scope.
struct BufferStateSuccess {
  bool advance = false;
};

std::expected<BufferStateSuccess, Rejected> validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost);

// Apply one planner-sanctioned action token at the current cursor. Accepts both
// Transform-start tokens (in `transformResult.resultsAt(cursor)`) and
// Composition activation tokens (insertion entry chars, text-object
// structurals, J — looked up against CompositionStrategies for the diff).
// Full live sequences with typed payloads are handled by acceptSnapshot.
// Simulates via the canonical Vim interpreter (Edit::applyEdit) so the
// post-state is identical to live Vim execution.
//
//   postLines / postCursor / postMode  : simulated post-token state.
//   reachesPostFencepost               : post-state matches the planned
//                                        post-fencepost (caller advances
//                                        to the next edit).
//   entersInsert                       : token transitioned to Insert mode
//                                        (caller switches to Insert phase).
struct EditSuccess {
  Lines postLines;
  CursorPos postCursor{0, 0};
  Mode postMode = Mode::Normal;
  bool reachesPostFencepost = false;
  bool entersInsert = false;
};

std::expected<EditSuccess, Rejected> applyEdit(
    const PlannedEditView& pedView,
    const Lines& currentLines,
    CursorPos cursor,
    std::string_view text,
    const CompositionOptimizerParams& params,
    const Config& config);

}  // namespace Explore::EditHandler
