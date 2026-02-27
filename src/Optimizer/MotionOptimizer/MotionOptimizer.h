#pragma once

#include <string_view>
#include <vector>

#include "MotionOptimizerParams.h"

#include "Optimizer/OptimizerResult.h"
#include "Optimizer/RangeResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/MotionBoundary.h"
#include "Keyboard/Config.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

#include "BufferIndex.h"

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

  // startingEffort was removed: benchmark (StartingEffortTradeoffTest.cpp) showed
  // Jaccard similarity >=0.988 and 100% best-result overlap with vs without prior
  // effort context, so the A* exploration order is not meaningfully affected.
  MotionOptimizer(const Config& config) : config(config) {}

  // Returns results and search statistics
  // ~ O(n^2)
  // Note: Internally dispatches to optimizeImpl<Forward> based on initialPos vs goalPos
  MotionResult optimize(
    // Core information
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,

    // Search tuning (can adjust with designated initializers)
    MotionOptimizerParams params = {},
    std::string_view userSequence = "", // What the user typed, which informs stopping point.

    // Continuation from broader context
    const MotionBoundary& parentBoundary = MotionBoundary(),

    // Niche settings
    const NavContext& navigationContext = NavContext()
  );


  // Multi-sink movement optimization: find paths to any position in [rangeBegin, rangeEnd)
  // Returns up to params.maxResults unique end positions (or total paths if allowMultiplePerPosition).
  // Precondition: initialPos must NOT be in [rangeBegin, rangeEnd) (nothing to optimize)

  // Simple wrapper to forward constructed BufferIndex
  RangeMotionResult optimizeToRange(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& rangeBegin,
    const CursorPos& rangeEnd,
    MotionOptimizerRangeParams params = {},
    std::string_view userSequence = "",
    const MotionBoundary& boundary = MotionBoundary(),
    const NavContext& navigationContext = NavContext()
  );

  // Overload with caller-provided BufferIndex
  RangeMotionResult optimizeToRange(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& rangeBegin,
    const CursorPos& rangeEnd,
    MotionOptimizerRangeParams params,
    std::string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navigationContext,
    BufferIndexRef bufferRef
  );

private:
  // Templated implementations after delegation
  template<bool Forward>
  MotionResult optimizeImpl(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
    std::string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerParams params
  );

  template<bool Forward>
  RangeMotionResult optimizeToRangeImpl(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& rangeBegin,
    const CursorPos& rangeEnd,
    std::string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerRangeParams params,
    const BufferIndex& bufferIndex,
    int lineOffset
  );
};
