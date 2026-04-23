#pragma once

#include <vector>

#include "DiffState.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

struct CompositionPlan {
  CursorPos finalGoalPos{0, 0};
  std::vector<DiffState> diffs;
  std::vector<Lines> fenceposts;

  int totalEdits() const { return static_cast<int>(diffs.size()); }

  bool empty() const { return diffs.empty(); }

  const DiffState& diffAt(int editIndex) const {
    return diffs[static_cast<size_t>(editIndex)];
  }

  const Lines& fencepostAt(int editIndex) const {
    return fenceposts[static_cast<size_t>(editIndex)];
  }
};
