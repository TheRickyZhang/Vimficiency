#pragma once

#include <ostream>
#include <vector>

#include "DiffState.h"
#include "Keyboard/Config.h"
#include "Types/Lines.h"

namespace DiffAlgorithm {

inline constexpr int Myers = 0;
inline constexpr int Tree = 1;
inline constexpr int Char = 2;

const char* name(int algorithm);

} // namespace DiffAlgorithm

namespace TreeDiff {

struct CostOptions {
  // Fixed per-region overhead beyond the operator (mode entry, etc.), ~1 — the
  // operator/distance cost lives in movement + deletion. At this value a nested
  // edit at a token seam can fragment (e.g. ((b))->(X) splits instead of one
  // (b)->X); see dev/optimizer/diff-generation.md § Seam fragmentation for the
  // deferred char-granular fix that would remove it.
  double diffOpenPenalty = 1.0;
  // Calibrates keystroke movement/deletion against inserted-text effort. Both
  // are now in keystroke units (matching insert effort), so this is ~1.
  double moveDeleteScale = 1.0;
};

std::vector<DiffState> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

// Per-region cost the planner actually weighed: delete, insert, and movement to
// reach this region from the previous one (the first region's movement is 0 —
// the leading run is free). Penalty is diffOpenPenalty per region (implied).
struct RegionBreakdown {
  DiffState diff;
  double del = 0.0;
  double ins = 0.0;
  double move = 0.0;
};

// Breakdown over the PRE-merge regions (the units the DP decided over) and the
// planner total. `total == sum over regions of (diffOpenPenalty + del + ins +
// move)` is checked inside. Diagnostic surface (approval tests); `calculate`
// does not use it, so the fast path pays nothing.
struct CostBreakdown {
  std::vector<RegionBreakdown> regions;
  double total = 0.0;
};

CostBreakdown calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

void formatDiffs(std::ostream& out,
                 const std::vector<DiffState>& diffs,
                 const Lines& initialLines);

} // namespace TreeDiff
