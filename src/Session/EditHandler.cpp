#include "EditHandler.h"

#include <span>

using namespace std;

namespace Explore::EditHandler {

std::expected<BufferStateSuccess, Rejected> validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost) {
  if (newLines == nextFencepost) {
    return BufferStateSuccess{.advance = true};
  }
  if (newLines == currentFencepost) {
    return BufferStateSuccess{.advance = false};
  }
  return std::unexpected(
      Rejected{"buffer state drifted outside the planned edit scope"});
}

std::expected<EditSuccess, Rejected> applyEdit(
    const EditResult& editResult,
    CursorPos cursor,
    std::string_view text) {
  if (text.empty()) {
    return std::unexpected(Rejected{"edit text must be non-empty"});
  }
  // MIRROR: match against EditResult::resultsAt — the planned edit-start set.
  std::span<const ::Result> starts = editResult.resultsAt(cursor.line, cursor.col);
  for (const ::Result& r : starts) {
    if (r.isValid() && r.getSequence().view() == text) {
      return EditSuccess{
          .postCursor = editResult.goalPosAt(cursor.line, cursor.col),
      };
    }
  }
  return std::unexpected(Rejected{
      "edit command is not part of the planned edit scope at this cursor"});
}

}  // namespace Explore::EditHandler
