#pragma once

#include <string_view>
#include <vector>

#include "MotionOptimizerParams.h"

#include "Optimizer/RangeResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/MotionBoundary.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"

#include "BufferIndex.h"

struct MotionResult {
  std::vector<Result> results;
  SearchStats stats;

  friend std::ostream& operator<<(std::ostream& os, const MotionResult& mr) {
    os << mr.stats << "\n";
    for (size_t i = 0; i < mr.results.size(); i++) {
      os << "  [" << i << "] " << mr.results[i] << "\n";
    }
    return os;
  }
};

struct RangeMotionResult {
  std::vector<RangeResult> results;
  SearchStats stats;

  friend std::ostream& operator<<(std::ostream& os, const RangeMotionResult& mr) {
    os << mr.stats << "\n";
    for (size_t i = 0; i < mr.results.size(); i++) {
      os << "  [" << i << "] " << mr.results[i] << "\n";
    }
    return os;
  }
};

struct MotionOptimizer {
  Config config;

  MotionOptimizer(const Config& config) : config(config) {}

  // For movement only. Builds index for faster movement computation.
  // TODO: Only RunningEffort is continued from pre-existing state, everything else can be fresh. (make sure that we set cost += newCost - previousCost, not cost = newCost!)

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
    const RunningEffort& startingEffort  = RunningEffort(),

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
    const RunningEffort& startingEffort = RunningEffort(),
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
    const RunningEffort& startingEffort,
    const NavContext& navigationContext,
    BufferIndexRef bufferRef
  );

private:
  // Templated single-goal implementation - Forward known at compile time
  template<bool Forward>
  MotionResult optimizeImpl(
    const Lines& lines,
    const CursorPos& initialPos,
    const RunningEffort& startingEffort,
    const CursorPos& goalPos,
    std::string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerParams params
  );

  // Templated range implementation - Forward known at compile time
  template<bool Forward>
  RangeMotionResult optimizeToRangeImpl(
    const Lines& lines,
    const CursorPos& initialPos,
    const RunningEffort& startingEffort,
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
