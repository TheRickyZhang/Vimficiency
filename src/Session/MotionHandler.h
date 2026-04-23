#pragma once

#include <string>
#include <string_view>
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

// Motion-side logic for Explore::Session. Stateless — the session passes in
// the current state each call, and the handler returns pure data.
//
// MIRROR boundary: anywhere we call simulateMotions, parseMotions, or getEffort
// is semantic-coupled to the interpreter/effort models and must be reviewed
// together when they change.
//
namespace Explore::MotionHandler {

// Per-action effect surfaced to the orchestrator. `accepted == false` carries
// a diagnostic in `rejectReason`; otherwise `newCursor` + `appendedSeq` tell
// the session what to fold into its next State snapshot.
struct MotionEffect {
  bool accepted = false;
  CursorPos newCursor{0, 0};
  std::string appendedSeq;
  std::string rejectReason;
};

// Parse `text`, simulate from `cursor`, report where the cursor lands.
MotionEffect applyMotion(
    const Lines& lines,
    CursorPos cursor,
    std::string_view text,
    const NavContext& navContext);

// Trust-me cursor sync. `rawKeys` is appended only if it parses as a motion
// sequence (keeps unknown bytes out of the session's seq/cost tokenizer).
MotionEffect acceptCursorMove(CursorPos newCursor, std::string_view rawKeys);

}  // namespace Explore::MotionHandler
