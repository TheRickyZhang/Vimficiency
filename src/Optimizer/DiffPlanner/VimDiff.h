#pragma once

#include <vector>

#include "DiffState.h"
#include "Keyboard/Config.h"
#include "Types/CountPrefixLimits.h"
#include "Types/Lines.h"

namespace VimDiff {

// DP budget summed over blocks in `(n+1)·(m+1)` single-plan cells (16 B each,
// two tables), so this bounds memory as well as time; a multi-plan run is
// charged by its larger cell. Over it `calculate` skips the DP and returns the
// sealed partition instead.
inline constexpr long long MAX_PLANNER_CELLS = 100'000'000;

// Upper bound on `CostOptions::maxPlans`: the multi-plan DP reserves this many
// candidate slots per cell.
inline constexpr int MAX_PLANS_CAP = 8;

struct CostOptions {
  double moveDeleteScale = 1.0;   // scales keystroke move/delete vs insert effort
  int maxPlans = 1;
  int maxPrefixCount = CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT;  // same knob as the searches
  long long maxPlannerCells = MAX_PLANNER_CELLS;
};

// One candidate partition and its planner cost.
struct Plan {
  std::vector<DiffState> diffs;
  double cost = 0.0;
};

// Up to `options.maxPlans` distinct partitions, ascending by planner cost —
// `front()` is the optimum. Empty when initial already equals goal. Over
// `options.maxPlannerCells`, or when a block spans more than 65535 chars on
// either side, the single plan is the sealed partition (one region per
// unmatched block), correct but not weighed against alternatives.
std::vector<Plan> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

// Per-region cost the planner weighed. `ins` is the insert phase: typed effort
// plus the ending <Esc>, plus the entry key only for a pure insertion — a
// deletion's change form absorbs it (0 when nothing is typed). `move` is the
// counted-motion traversal from the previous region's
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
