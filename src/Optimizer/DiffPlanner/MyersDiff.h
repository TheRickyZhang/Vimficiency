#pragma once

#include <vector>

#include "DiffState.h"
#include "Types/Lines.h"

// Historical character-level Myers shortest-edit-script diff. It remains useful
// as a fast baseline and fallback, but it is not the default Vim command planner.
namespace MyersDiff {

std::vector<DiffState> calculate(
    const Lines& initialLines,
    const Lines& goalLines);

Lines applyDiffState(
    const DiffState& diff,
    const Lines& lines);

Lines applyAllDiffState(
    const std::vector<DiffState>& diffs,
    const Lines& initialLines);

} // namespace MyersDiff
