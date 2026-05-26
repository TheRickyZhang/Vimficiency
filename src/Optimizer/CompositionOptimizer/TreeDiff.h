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
  double diffOpenPenalty = 8.0;
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
