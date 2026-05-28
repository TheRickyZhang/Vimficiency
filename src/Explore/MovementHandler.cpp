#include "MovementHandler.h"

#include "Interpreter/MovementInterpreter.h"

using namespace std;

namespace Explore::MovementHandler {

namespace {

expected<MotionSuccess, Rejected> finishMove(
    const Lines& lines,
    CursorPos newCursor,
    string appendedSeq,
    const NavBoundary& boundary) {
  if (!lines.contains(newCursor)) {
    return unexpected(Rejected{"motion landed outside the current buffer"});
  }
  const int lastLine = static_cast<int>(lines.size()) - 1;
  const int lastLineLength = static_cast<int>(lines[lastLine].size());
  if (!boundary.isPositionInBounds(newCursor, lastLine, lastLineLength)) {
    return unexpected(Rejected{"motion landed outside the allowed boundary"});
  }
  return MotionSuccess{
      .newCursor = newCursor,
      .appendedSeq = std::move(appendedSeq),
  };
}

}  // namespace

std::expected<MotionSuccess, Rejected> applyMovement(
    const Lines& lines, CursorPos cursor, std::string_view text,
    const NavBoundary& boundary,
    const NavContext& navContext) {
  if (text.empty()) {
    return std::unexpected(Rejected{"motion text must be non-empty"});
  }
  auto parsed = parseMovements(text);
  if (!parsed) {
    return std::unexpected(Rejected{
        "motion text failed to parse: " +
        formatMovementParseError(parsed.error())});
  }
  return finishMove(
      lines,
      simulateMovements(cursor, text, lines, navContext),
      string(text),
      boundary);
}

std::expected<MotionSuccess, Rejected> acceptCursorMove(
    const Lines& lines,
    CursorPos newCursor,
    std::string_view rawKeys,
    const NavBoundary& boundary) {
  return finishMove(lines, newCursor, string(rawKeys), boundary);
}

} // namespace Explore::MovementHandler
