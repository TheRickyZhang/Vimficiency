#pragma once

#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

struct EditFrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  const DiffState& diff;
  int maxCount = 0;
  // When false (default): only the cheapest item per unique goal position
  // is emitted — aligns with MotionFrontier's `allowMultiplePerPosition`
  // and lets the display layer trust the optimizer's output without
  // post-hoc filtering. When true, every emission path contributes.
  bool allowMultiplePerPosition = false;
};

struct EditFrontierItem {
  std::string fullSequence;
  std::string molecule;
  std::string typedText;
  CursorPos goalPos{0, 0};
  double cost = 0.0;
};

std::vector<EditFrontierItem> rankEditFrontier(
    const EditFrontierQuery& query,
    const Config& config);
