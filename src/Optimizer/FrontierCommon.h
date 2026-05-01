#pragma once

// =============================================================================
// Frontier base aggregates
// =============================================================================
// Shared fields for the depth-1 frontier types in NavOptimizer/ and
// TransformOptimizer/. Each frontier emits its own derived item / query;
// what's common (`token` + `goalPos` + `cost` for items, and the input
// `lines` / `cursor` / `maxCount` / `allowMultiplePerPosition` for queries)
// lives here so the divergence is visible at the inheritance line.

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Token.h"

struct FrontierItem {
  Token token;
  CursorPos goalPos{0, 0};
  // Raw effort of this single token (not a cumulative path cost).
  double cost = 0.0;
};

struct FrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  int maxCount = 0;
  // Per-frontier semantics — see NavFrontier.h / TransformFrontier.h for
  // how each one interprets dedup. The flag's wire-meaning differs by
  // frontier kind; only the field is shared.
  bool allowMultiplePerPosition = false;
};
