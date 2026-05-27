#pragma once

#include "Types/CursorPos.h"
#include "Types/Sequence.h"

// Composition-owned join escape hatch. The first action must be `J`; plans
// whose useful work starts with a residual edit belong to TransformOptimizer.
struct JoinPlan {
  Sequence sequence;
  CursorPos goalPos;
  double effort;
  int entryLine;
};
