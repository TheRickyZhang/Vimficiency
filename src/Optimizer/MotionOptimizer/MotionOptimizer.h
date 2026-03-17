#pragma once

#include <string_view>
#include <vector>

#include "MotionOptimizerParams.h"
#include "BufferIndex.h"

#include "Optimizer/OptimizerResult.h"
#include "Optimizer/RangeResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/MotionBoundary.h"
#include "Keyboard/Config.h"
#include "Types/CharInterval.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"


// Generally MotionResult / RangeMotinoResult are coupled with MotionOptimizer return results, but if there are cases where they aren't, consider isolating these declarations
struct MotionResult : BaseOptimizerResult<> {
  const MotionSearchStats& getStats() const { return stats_; }

private:
  MotionSearchStats stats_;
  friend struct MotionOptimizer;
  MotionResult(std::vector<Result> results, MotionSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct RangeMotionResult : BaseOptimizerResult<RangeResult> {
  const MotionSearchStats& getStats() const { return stats_; }

private:
  MotionSearchStats stats_;
  friend struct MotionOptimizer;
  RangeMotionResult(std::vector<RangeResult> results, MotionSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct MotionOptimizer {
  Config config;

  MotionOptimizer(const Config& config) : config(config) {}

  // For single position goal
  MotionResult optimize(
    // Core information
    const Lines& lines, const CursorPos& initialPos, const CursorPos& goalPos,
    // Search tuning (can adjust with designated initializers)
    MotionOptimizerParams params = {}, std::string_view userSequence = "",
    // Continuation from broader context
    const MotionBoundary& parentBoundary = MotionBoundary(),
    // Niche settings
    const NavContext& navigationContext = NavContext()
  );

  // Multi-sink movement optimization: find paths to any position in the target interval.
  // Interval is character-wise [first, last] (inclusive).
  // If caller data is half-open [begin, end), convert at call boundary
  // before invoking this API.
  // Precondition: initialPos must NOT be in the target interval.
  RangeMotionResult optimizeToRange(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& range,
    MotionOptimizerRangeParams params = {}, std::string_view userSequence = "",
    const MotionBoundary& boundary = MotionBoundary(),
    const NavContext& navigationContext = NavContext()
  );

  RangeMotionResult optimizeToRange(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& range,
    MotionOptimizerRangeParams params, std::string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navigationContext,
    const BufferIndex& bufferIndex, int lineOffset
  );

};
