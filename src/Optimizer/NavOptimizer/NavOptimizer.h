#pragma once

#include <string_view>
#include <vector>

#include "NavOptimizerParams.h"
#include "BufferIndex.h"

#include "Optimizer/OptimizerResult.h"
#include "Optimizer/RangeResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Types/CharInterval.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"


// Generally NavResult / RangeMotinoResult are coupled with NavOptimizer return results, but if there are cases where they aren't, consider isolating these declarations
struct NavResult : BaseOptimizerResult<> {
  const NavSearchStats& getStats() const { return stats_; }

private:
  NavSearchStats stats_;
  friend struct NavOptimizer;
  NavResult(std::vector<Result> results, NavSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct RangeNavResult : BaseOptimizerResult<RangeResult> {
  const NavSearchStats& getStats() const { return stats_; }

private:
  NavSearchStats stats_;
  friend struct NavOptimizer;
  RangeNavResult(std::vector<RangeResult> results, NavSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct NavOptimizer {
  Config config;

  NavOptimizer(const Config& config) : config(config) {}

  // For single position goal
  NavResult optimize(
    // Core information
    const Lines& lines, const CursorPos& initialPos, const CursorPos& goalPos,
    // Search tuning (can adjust with designated initializers)
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    // Continuation from broader context
    const NavBoundary& parentBoundary = NavBoundary(),
    // Niche settings
    const NavContext& navigationContext = NavContext()
  );

  // Multi-sink movement optimization: find paths to any position in the target interval.
  // Interval is character-wise [first, last] (inclusive).
  // If caller data is half-open [begin, end), convert at call boundary
  // before invoking this API.
  // Precondition: initialPos must NOT be in the target interval.
  RangeNavResult optimizeToRange(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& range,
    NavOptimizerRangeParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext()
  );

  RangeNavResult optimizeToRange(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& range,
    NavOptimizerRangeParams params, std::string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navigationContext,
    const BufferIndex& bufferIndex, int lineOffset
  );

};
