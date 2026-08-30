#pragma once

#include <vector>

#include "DiffState.h"
#include "Keyboard/Config.h"
#include "Types/CountPrefixLimits.h"
#include "Types/Lines.h"

namespace VimDiff {

struct CostOptions {
  double moveDeleteScale = 1.0;   // scales keystroke move/delete vs insert effort
  int maxPlans = 1;
  int maxPrefixCount = CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT;  // same knob as the searches
};

// One candidate partition and its planner cost.
struct Plan {
  std::vector<DiffState> diffs;
  double cost = 0.0;
};

// Up to `options.maxPlans` distinct partitions, ascending by planner cost —
// `front()` is the optimum. Empty when initial already equals goal.
std::vector<Plan> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

// Per-region cost the planner weighed. `ins` is the insert phase: typed effort
// plus the insert-mode entry key and the ending <Esc> (0 when nothing is
// typed). `move` is the counted-motion traversal from the previous region's
// initial end to this region's initial begin (0 for the first).
// Because VimDiff has no merge/refine pass, `total` equals the DP optimum
// exactly. Diagnostic surface — `calculate` does not use it.
struct RegionBreakdown {
  DiffState diff;
  double del = 0.0;
  double ins = 0.0;
  double move = 0.0;
};

struct CostBreakdown {
  std::vector<RegionBreakdown> regions;
  double total = 0.0;
};

// One breakdown per plan from `calculate`, same ordering. Each breakdown's
// `total` equals that plan's `cost`. Diagnostic surface for inspecting the
// alternatives the DP weighed against the optimum.
std::vector<CostBreakdown> calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

}  // namespace VimDiff
