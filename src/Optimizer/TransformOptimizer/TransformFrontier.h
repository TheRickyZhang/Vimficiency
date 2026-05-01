#pragma once

#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/FrontierCommon.h"

// Default dedup (`allowMultiplePerPosition == false`): duplicate edit
// sequences are collapsed — each distinct command shape (`rm`, `sm<Esc>`,
// `cl m<Esc>`, ...) surfaces once. Distinct strategies that happen to land
// at the same cursor cell are preserved; for edits the command shape is
// the pedagogical point, not the landing position.
//
// NOTE: this differs from `NavFrontier::allowMultiplePerPosition`, which
// dedups by landing cell. Edits and motions deliberately diverge here —
// see TransformFrontier.cpp:EditEmitter for the rationale.
struct TransformFrontierQuery : FrontierQuery {
  const DiffState& diff;
};

struct TransformFrontierItem : FrontierItem {
  std::string typedText;
};

std::vector<TransformFrontierItem> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config);
