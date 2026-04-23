#include "MotionHandler.h"

#include "Interpreter/MotionInterpreter.h"

using namespace std;

namespace Explore::MotionHandler {

MotionEffect applyMotion(
    const Lines& lines,
    CursorPos cursor,
    std::string_view text,
    const NavContext& navContext) {
  MotionEffect eff;
  if (text.empty()) {
    eff.rejectReason = "motion text must be non-empty";
    return eff;
  }
  auto parsed = parseMotions(text);
  if (!parsed) {
    eff.rejectReason = "motion text failed to parse: " + formatMotionParseError(parsed.error());
    return eff;
  }
  eff.accepted = true;
  eff.newCursor = simulateMotions(cursor, text, lines, navContext);
  eff.appendedSeq = string(text);
  return eff;
}

MotionEffect acceptCursorMove(CursorPos newCursor, std::string_view rawKeys) {
  MotionEffect eff;
  eff.accepted = true;
  eff.newCursor = newCursor;
  if (!rawKeys.empty() && parseMotions(rawKeys)) {
    eff.appendedSeq = string(rawKeys);
  }
  return eff;
}

}  // namespace Explore::MotionHandler
