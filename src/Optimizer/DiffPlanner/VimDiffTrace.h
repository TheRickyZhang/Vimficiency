#pragma once

// Read-only view of one planner run: the sealed blocks, every DP cell, and the
// cells on the optimal path. Diagnostic surface for the docs-site widgets and
// tests/Debug/VimDiffTraceExport; `calculate` does not use it.

#include <cstdint>
#include <vector>

#include "SealMatchedRuns.h"
#include "VimDiff.h"

namespace VimDiff {

// Three step families: move (MOVE within a block, CROSS over a seal), delete
// (DELETE, or CHANGE when the `c` form lands straight in insert mode and so
// skips the entry key) and type (ENTER from normal mode, TYPE within insert
// mode). EXIT is the free edge back to normal mode; LEADING is the root.
enum Step : int8_t { LEADING, MOVE, CROSS, DELETE, CHANGE, ENTER, TYPE, EXIT };

struct TraceCell {
  double cost = INF;  // INF: unreached
  Step step = LEADING;
  int pi = 0, pj = 0;  // predecessor; in the previous block for CROSS, in `in` for TYPE/EXIT
};

// out[i, j]: normal mode, initial [0,i) consumed, goal [0,j) produced;
// in[i, j]: insert mode. Cell (i, j) sits at index i * (m + 1) + j.
struct TraceBlock {
  Block block;
  int lead = 0, trail = 0;  // matched diagonal length at the block's start / end
  std::vector<TraceCell> out, in;

  const TraceCell& outAt(int i, int j) const { return out[i * (block.m() + 1) + j]; }
  const TraceCell& inAt(int i, int j) const { return in[i * (block.m() + 1) + j]; }
};

struct TracePathCell {
  int block, i, j;
  bool inTable;
};

struct Trace {
  std::vector<TraceBlock> blocks;
  std::vector<TracePathCell> path;  // root first, optimum last; empty for the sealed fallback
  long long dpCells = 0, rawCells = 0;  // Σ(n+1)(m+1) over blocks vs (N+1)(M+1)
  bool sealedFallback = false;
  CostBreakdown breakdown;
};

Trace trace(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

}  // namespace VimDiff
