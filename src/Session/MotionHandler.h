#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

// Motion-side logic for Explore::View. Currently stateless: simply takes in state, returns data.

// MIRROR boundary: anywhere we call simulateMotions, parseMotions, or getEffort
// is semantic-coupled to the interpreter/effort models and must be reviewed together when changed
namespace Explore::MotionHandler {

struct MotionSuccess {
  CursorPos newCursor{0, 0};
  std::string appendedSeq;
};

// Parse `text`, simulate from `cursor`, report where the cursor lands.
std::expected<MotionSuccess, Rejected> applyMotion(
    const Lines& lines,
    CursorPos cursor,
    std::string_view text,
    const NavContext& navContext);

// Trust-me cursor sync. `rawKeys` is appended only if it parses as a motion
// sequence (keeps unknown bytes out of the view's seq/cost tokenizer).
// Cannot fail.
MotionSuccess acceptCursorMove(CursorPos newCursor, std::string_view rawKeys);

}  // namespace Explore::MotionHandler
