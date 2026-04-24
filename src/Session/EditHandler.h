#pragma once

#include <expected>
#include <string_view>

#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

// Transform-side logic for Explore::View. Historical Edit naming retained
// because this layer consumes EditResult from the current codebase.
//
// MIRROR boundary: everything that reads EditResult::resultsAt / goalPosAt is
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

// Full-sequence apply path. Validates `text` against the planned edit-start
// set for `cursor`; returns the per-start goal cursor on accept.
struct EditSuccess {
  CursorPos postCursor{0, 0};
};

std::expected<EditSuccess, Rejected> applyEdit(
    const EditResult& editResult,
    CursorPos cursor,
    std::string_view text);

}  // namespace Explore::EditHandler
