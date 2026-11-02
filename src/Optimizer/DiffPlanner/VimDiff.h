#pragma once

#include <vector>

#include "DiffState.h"
#include "Keyboard/Config.h"
#include "Types/CountPrefixLimits.h"
#include "Types/Lines.h"

namespace VimDiff {

// DP budget summed over blocks in `(n+1)·(m+1)` cells (16 B each, two tables),
// so this bounds memory as well as time. Over it `calculate` skips the DP and
// returns the sealed partition instead.
inline constexpr long long MAX_PLANNER_CELLS = 100'000'000;

struct CostOptions {
  double moveDeleteScale = 1.0;   // scales keystroke move/delete vs insert effort
  int maxPrefixCount = CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT;  // same knob as the searches
  long long maxPlannerCells = MAX_PLANNER_CELLS;
};

// The cheapest partition and its planner cost. `diffs` is empty when initial
// already equals goal. Over `options.maxPlannerCells`, or when a block spans
// more than 65535 chars on either side, the plan is the sealed partition (one
// region per unmatched block), correct but not weighed against alternatives.
struct Plan {
  std::vector<DiffState> diffs;
  double cost = 0.0;
};

Plan calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

// Per-region cost the planner weighed. `ins` is the insert phase: typed effort
// plus the ending <Esc>, plus the entry key only for a pure insertion — a
// deletion's change form absorbs it (0 when nothing is typed). `move` is the
// counted-motion traversal from the previous region's initial end to this
// region's initial begin (0 for the first). `total` equals the DP optimum
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

CostBreakdown calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

}  // namespace VimDiff
