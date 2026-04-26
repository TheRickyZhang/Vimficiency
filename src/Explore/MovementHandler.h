#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "Boundary/NavBoundary.h"
#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

// Motion-side logic for Explore::View. Currently stateless: simply takes in state, returns data.

// MIRROR boundary: anywhere we call simulateMovements, parseMovements, or getEffort
// is semantic-coupled to the interpreter/effort models and must be reviewed together when changed
namespace Explore::MovementHandler {

struct MotionSuccess {
  CursorPos newCursor{0, 0};
  std::string appendedSeq;
};

// Parse `text`, simulate from `cursor`, report where the cursor lands.
std::expected<MotionSuccess, Rejected> applyMovement(
    const Lines& lines,
    CursorPos cursor,
    std::string_view text,
    const NavBoundary& boundary,
    const NavContext& navContext);

// Trust-me cursor sync. `rawKeys` is appended only if it parses as a motion
// sequence (keeps unknown bytes out of the view's seq/cost tokenizer).
std::expected<MotionSuccess, Rejected> acceptCursorMove(
    const Lines& lines,
    CursorPos oldCursor,
    CursorPos newCursor,
    std::string_view rawKeys,
    const NavBoundary& boundary,
    const NavContext& navContext);

}  // namespace Explore::MovementHandler
