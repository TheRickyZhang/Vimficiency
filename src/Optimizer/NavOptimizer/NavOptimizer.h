#pragma once

#include <string_view>
#include <vector>

#include "NavOptimizerParams.h"
#include "BufferIndex.h"

#include "Optimizer/OptimizerResult.h"
#include "Optimizer/LandingResult.h"
#include "Optimizer/SearchControl.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Types/CharInterval.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"


// All NavOptimizer searches return `LandingNavResult` — each result carries
// its landing position via `LandingResult::getGoalPos()`. The single-cursor
// overload is a convenience wrapper that forwards to the range overload with
// a 1-cell `CharInterval(goalPos, goalPos)`; every result's landing is
// therefore the input `goalPos`.
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

  // Find sequences from initialPos to some positions in goalInterval.
  // You usually want to pass params.maxResultsPerEndPos > 1.
  // `control` (optional) is polled each pop; the search returns best-so-far
  // when it fires.
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& goalInterval,
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext(),
    const SearchControl* control = nullptr
  );

  // Same overload but takes a pre-built BufferIndex
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& goalInterval,
    NavOptimizerParams params, std::string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navigationContext,
    const BufferIndex& bufferIndex, int lineOffset,
    const SearchControl* control = nullptr
  );

  // single-cursor goal overload, underlying implementation is the same
  // In most cases you want to pass params.maxResultsPerEndPos > 1
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CursorPos& goalPos,
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext(),
    const SearchControl* control = nullptr
  );
};
