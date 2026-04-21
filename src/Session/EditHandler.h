#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Session/Explore.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

// Edit-side logic for Explore::Session. Stateless.
//
// MIRROR boundary: everything that reads EditResult::resultsAt / goalPosAt is
// semantic-coupled to the composition/edit optimizer and must be reviewed
// together when those outputs change shape.
//
// CUSTOM logic:
//   - `extractAtom` splits the normal-mode prefix off a full edit sequence
//     via parseSequence (the explore UI surfaces atoms, not full sequences).
//   - `validateBufferState` encodes the explore-specific strict-revert policy:
//     accept iff the buffer matches either the current or next fencepost.

namespace Explore::EditHandler {

// Extract the normal-mode atom from a full edit sequence. `rm` / `x` stay
// whole; `sm<Esc>` collapses to `s`; `clm<Esc>` collapses to `cl`. Stops at
// the first TypedText/Escape token via parseSequence; falls back to the full
// sequence when parsing fails or yields an empty prefix.
std::string extractAtom(std::string_view fullSequence);

// Deduplicated atomic edit recommendations at `cursor`. Keeps the cheapest
// cost per atom. Landing cell is the edit's per-start goal position.
std::vector<Recommendation> recommendations(
    const EditResult& editResult,
    CursorPos cursor,
    int maxCount);

// Buffer-state validation under the strict (b) policy.
//   advance     == true  : newLines matches nextFencepost (advance phase).
//   accepted == true && advance == false : newLines matches currentFencepost
//                                          (no-op — native undo or re-sync).
//   accepted == false   : drift outside the planned edit scope → reject.
struct BufferStateEffect {
  bool accepted = false;
  bool advance = false;
  std::string rejectReason;
};

BufferStateEffect validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost);

// Full-sequence apply path. Validates `text` against the planned edit-start
// set for `cursor`; returns the per-start goal cursor on accept.
struct ApplyEditEffect {
  bool accepted = false;
  std::string rejectReason;
  CursorPos postCursor{0, 0};
};

ApplyEditEffect applyEdit(
    const EditResult& editResult,
    CursorPos cursor,
    std::string_view text);

}  // namespace Explore::EditHandler
