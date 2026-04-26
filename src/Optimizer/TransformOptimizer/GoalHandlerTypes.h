#pragma once

#include <optional>

#include "TransformState.h"

// Protocol types for GoalHandler — shared between the dispatcher and both
// goal handler implementations (DeletionGoalHandler, ChangeGoalHandler).

// on*Goal returns 1-2 EditStates for the dispatcher to emit.
struct GoalStates {
  TransformState primary;
  std::optional<TransformState> dotVariant;
};

// tryUseSuffixCache returns hit status and whether the start was capped.
struct SuffixCacheResult {
  bool hit = false;
  bool startCapped = false;
};
