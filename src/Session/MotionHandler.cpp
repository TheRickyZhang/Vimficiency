#include "MotionHandler.h"

#include "Interpreter/MotionInterpreter.h"

using namespace std;

namespace Explore::MotionHandler {

std::expected<MotionSuccess, Rejected> applyMotion(
    const Lines& lines, CursorPos cursor, std::string_view text,
    const NavContext& navContext) {
  if (text.empty()) {
    return std::unexpected(Rejected{"motion text must be non-empty"});
  }
  auto parsed = parseMotions(text);
  if (!parsed) {
    return std::unexpected(Rejected{
        "motion text failed to parse: " +
        formatMotionParseError(parsed.error())});
  }
  return MotionSuccess{
      .newCursor = simulateMotions(cursor, text, lines, navContext),
      .appendedSeq = string(text),
  };
}

MotionSuccess acceptCursorMove(CursorPos newCursor, std::string_view rawKeys) {
  MotionSuccess out;
  out.newCursor = newCursor;
  if (!rawKeys.empty() && parseMotions(rawKeys)) {
    out.appendedSeq = string(rawKeys);
  }
  return out;
}

} // namespace Explore::MotionHandler
