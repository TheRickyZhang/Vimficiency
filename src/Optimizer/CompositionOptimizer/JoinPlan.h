#pragma once

#include "Types/CursorPos.h"
#include "Types/Sequence.h"

// A pre-computed plan to join source lines using the J command,
// optionally followed by residual edits on the joined result.
// Offered as an alternative path in the CompositionOptimizer A* search.
struct JoinPlan {
  Sequence sequence;   // Full assembled sequence (J's + residuals + j's)
  CursorPos goalPos;    // Cursor position after plan executes
  double effort;       // Pre-computed effort
  int entryLine;       // Cursor must be on this line (any column)
};
