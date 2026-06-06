#pragma once

#include <ostream>
#include <vector>

#include "DiffState.h"
#include "Keyboard/Config.h"
#include "Types/Lines.h"

namespace DiffAlgorithm {

inline constexpr int Myers = 0;
inline constexpr int Tree = 1;

const char* name(int algorithm);

} // namespace DiffAlgorithm

namespace TreeDiff {

struct CostOptions {
  // Fixed per-region overhead beyond the operator (mode entry, etc.). Distance
  // is carried by movement and the operator key by deletion. ~2: large enough
  // that the recurse won't fragment a nested edit at a token seam (e.g. keep the
  // outer parens of ((b))->(X) as one (b)->X region), small enough not to merge
  // genuinely separate edits.
  double diffOpenPenalty = 2.0;
  // Calibrates keystroke movement/deletion against inserted-text effort. Both
  // are now in keystroke units (matching insert effort), so this is ~1.
  double moveDeleteScale = 1.0;
};

std::vector<DiffState> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

void formatDiffs(std::ostream& out,
                 const std::vector<DiffState>& diffs,
                 const Lines& initialLines);

} // namespace TreeDiff
