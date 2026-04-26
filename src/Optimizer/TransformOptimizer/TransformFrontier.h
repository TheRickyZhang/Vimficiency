#pragma once

#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

struct TransformFrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  const DiffState& diff;
  int maxCount = 0;
  // When false (default): duplicate edit sequences are collapsed — each
  // distinct command shape (`rm`, `sm<Esc>`, `cl m<Esc>`, ...) surfaces
  // once. Distinct strategies that happen to land at the same cursor cell
  // are preserved; for edits the command shape is the pedagogical point,
  // not the landing position.
  //
  // NOTE: this differs from `NavFrontier::allowMultiplePerPosition`,
  // which dedups by landing cell. Edits and motions deliberately diverge
  // here — see TransformFrontier.cpp:EditEmitter for the rationale.
  //
  // When true: every emission path contributes, even literal duplicates.
  bool allowMultiplePerPosition = false;
};

struct TransformFrontierItem {
  std::string fullSequence;
  std::string molecule;
  std::string typedText;
  CursorPos goalPos{0, 0};
  double cost = 0.0;
};

std::vector<TransformFrontierItem> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config);
