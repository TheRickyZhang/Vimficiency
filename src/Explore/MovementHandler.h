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

// Cursor sync from live Vim state. `rawKeys` is recorded as typed input.
std::expected<MotionSuccess, Rejected> acceptCursorMove(
    const Lines& lines,
    CursorPos newCursor,
    std::string_view rawKeys,
    const NavBoundary& boundary);

}  // namespace Explore::MovementHandler
