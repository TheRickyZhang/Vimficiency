#pragma once

// =============================================================================
// Frontier base aggregates
// =============================================================================
// Shared fields for the depth-1 frontier types in NavOptimizer/ and
// TransformOptimizer/. Each frontier emits its own derived item / query;
// what's common (`token` + `landingPos` + `costDiff` for items, and the input
// `lines` / `cursor` / accepted sequence context / recommendation limits
// for queries) lives here so the divergence is visible at the inheritance line.

#include <string_view>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Token.h"

class OptimizerParamOverrides;

// What the possible results we get are
struct Suggestion {
  Token token;
  CursorPos landingPos{0, 0};
  double costDiff = 0.0;
};

// What we currently are at
// Maybe better name: ExploreStatus
struct FrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  std::string_view seq = {};
  int maxCount = 0;
  // Optional override blob, applied to the constructed optimizer params
  // before search. Null = use whatever the C++ default is for every
  // field. See OptimizerParamOverrides.h.
  const OptimizerParamOverrides* overrides = nullptr;
};
