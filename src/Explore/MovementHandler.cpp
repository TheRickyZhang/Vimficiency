#include "MovementHandler.h"

#include "Interpreter/MovementInterpreter.h"

using namespace std;

namespace Explore::MovementHandler {

namespace {

bool isCursorOnConcreteBufferCell(const Lines& lines, const CursorPos& pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return false;
  const int maxCol = lines[pos.line].empty()
      ? 0
      : static_cast<int>(lines[pos.line].size()) - 1;
  return pos.col >= 0 && pos.col <= maxCol;
}

expected<MotionSuccess, Rejected> finishMove(
    const Lines& lines,
    CursorPos newCursor,
    string appendedSeq,
    const NavBoundary& boundary) {
  if (!isCursorOnConcreteBufferCell(lines, newCursor)) {
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
    CursorPos oldCursor,
    CursorPos newCursor,
    std::string_view rawKeys,
    const NavBoundary& boundary,
    const NavContext& navContext) {
  string appendedSeq;
  if (!rawKeys.empty()) {
    auto parsed = parseMovements(rawKeys);
    if (!parsed) {
      return unexpected(Rejected{
          "raw motion keys failed to parse: " +
          formatMovementParseError(parsed.error())});
    }
    const CursorPos simulated = simulateMovements(oldCursor, rawKeys, lines, navContext);
    if (simulated != newCursor) {
      return unexpected(Rejected{
          "raw motion keys did not produce the observed cursor move"});
    }
    appendedSeq = string(rawKeys);
  }
  return finishMove(lines, newCursor, std::move(appendedSeq), boundary);
}

} // namespace Explore::MovementHandler
