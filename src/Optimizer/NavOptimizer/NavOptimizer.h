#pragma once

#include <string_view>
#include <vector>

#include "NavOptimizerParams.h"
#include "BufferIndex.h"

#include "Optimizer/OptimizerResult.h"
#include "Optimizer/LandingResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Types/CharInterval.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"


// Two return types — the per-result element shape mirrors what the caller
// asked for:
//   - `NavResult` for single-cursor searches: each result is a `Result`
//     (sequence + cost). Per-result landing would just echo the input
//     `goalPos`, so it's omitted.
//   - `LandingNavResult` for CharInterval searches: each result is a
//     `LandingResult` (sequence + cost + landing position) because
//     different motions land in different cells of the interval.
// Both wrap the same internal A* implementation.
struct NavResult : BaseOptimizerResult<Result> {
  const NavSearchStats& getStats() const { return stats_; }

private:
  NavSearchStats stats_;
  friend struct NavOptimizer;
  NavResult(std::vector<Result> results, NavSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct LandingNavResult : BaseOptimizerResult<LandingResult> {
  const NavSearchStats& getStats() const { return stats_; }

private:
  NavSearchStats stats_;
  friend struct NavOptimizer;
  LandingNavResult(std::vector<LandingResult> results, NavSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct NavOptimizer {
  Config config;

  NavOptimizer(const Config& config) : config(config) {}

  // Multi-sink shortest path: motion sequences from `initialPos` to any
  // position in `goalInterval` (inclusive [first, last]). Each result
  // carries its landing position because different motions land in
  // different cells.
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& goalInterval,
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext()
  );

  // Same as the CharInterval overload but takes a pre-built BufferIndex
  // covering some sub-line range (for callers that have already computed one).
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& goalInterval,
    NavOptimizerParams params, std::string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navigationContext,
    const BufferIndex& bufferIndex, int lineOffset
  );

  // Single-cursor goal: distinct paths to one fixed point. Internally runs
  // the CharInterval search with a 1-element interval and
  // `allowMultiplePerPosition = true`, then strips the redundant per-result
  // landing position before returning. Caller already knows the landing —
  // it's the input `goalPos`.
  NavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CursorPos& goalPos,
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext()
  );
};
