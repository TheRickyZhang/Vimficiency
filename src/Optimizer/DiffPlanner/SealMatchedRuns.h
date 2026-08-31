#pragma once

#include <vector>

#include "PlannerCosts.h"
#include "VimDiff.h"

namespace VimDiff {

// Raw spans of one alignment block. Consecutive blocks are separated by a sealed
// run that no optimal plan edits into, so deletions never cross it.
struct Block {
  int aBegin, aEnd, bBegin, bEnd;
  int n() const { return aEnd - aBegin; }
  int m() const { return bEnd - bBegin; }
};

// Splits the alignment at every Myers-matched run that is cheaper to move over
// than to retype, keeping a margin inside the blocks where retyping still wins:
//   gate:   type(core) > move(core) + entry+<Esc> + stopSlack + startSlack
//   margin: keep depth d while ins(d) <= edge slack + move saving(d)
// Text outside the blocks is identical on both sides at the same offsets.
std::vector<Block> sealMatchedRuns(const FlatText& initial, const FlatText& goal,
                                   const Typing& typing, const Lines& initialLines,
                                   const Lines& goalLines, const Config& config,
                                   const CostOptions& options);

}  // namespace VimDiff
