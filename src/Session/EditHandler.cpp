#include "EditHandler.h"

#include <span>

using namespace std;

namespace Explore::EditHandler {

BufferStateEffect validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost) {
  BufferStateEffect eff;
  if (newLines == nextFencepost) {
    eff.accepted = true;
    eff.advance = true;
    return eff;
  }
  if (newLines == currentFencepost) {
    eff.accepted = true;
    eff.advance = false;
    return eff;
  }
  eff.rejectReason = "buffer state drifted outside the planned edit scope";
  return eff;
}

ApplyEditEffect applyEdit(
    const EditResult& editResult,
    CursorPos cursor,
    std::string_view text) {
  ApplyEditEffect eff;
  if (text.empty()) {
    eff.rejectReason = "edit text must be non-empty";
    return eff;
  }
  // MIRROR: match against EditResult::resultsAt — the planned edit-start set.
  std::span<const ::Result> starts = editResult.resultsAt(cursor.line, cursor.col);
  for (const ::Result& r : starts) {
    if (r.isValid() && r.getSequence().view() == text) {
      eff.accepted = true;
      eff.postCursor = editResult.goalPosAt(cursor.line, cursor.col);
      return eff;
    }
  }
  eff.rejectReason = "edit command is not part of the planned edit scope at this cursor";
  return eff;
}

}  // namespace Explore::EditHandler
