#pragma once

#include <string_view>
#include <vector>

#include "MotionOptimizerParams.h"

#include "Optimizer/Config.h"
#include "Optimizer/RangeResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/MotionBoundary.h"
#include "VimTypes/NavContext.h"
#include "VimTypes/Position.h"
#include "State/RunningEffort.h"
#include "VimTypes/Lines.h"

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
    const Position& initialPos,
    const Position& goalPos,

    // Search tuning (can adjust with designated initializers)
    MotionOptimizerParams params = {},
    std::string_view userSequence = "", // What the user typed, which informs stopping point.

    // Continuation from broader context
    const MotionBoundary& parentBoundary = MotionBoundary(),
    const RunningEffort& startingEffort  = RunningEffort(),

    // Niche settings
    const NavContext& navigationContext = NavContext()
  );


  // Multi-sink movement optimization: find paths to any position in [rangeFirst, rangeEnd)
  // Returns up to params.maxResults unique end positions (or total paths if allowMultiplePerPosition).
  // Precondition: initialPos must NOT be in [rangeFirst, rangeEnd) (nothing to optimize)
  // Note: Internally dispatches to optimizeToRangeImpl<Forward> based on initialPos vs range
  RangeMotionResult optimizeToRange(
    // Core information
    const Lines& lines,
    const Position& initialPos,
    const Position& rangeFirst,
    const Position& rangeEnd,

    // Search tuning
    MotionOptimizerRangeParams params = {},
    std::string_view userSequence = "",

    // Continuation from broader context
    const MotionBoundary& boundary = MotionBoundary(),
    const RunningEffort& startingEffort = RunningEffort(),

    // Niche settings
    const NavContext& navigationContext = NavContext()
  );

private:
  // Templated implementation - Forward known at compile time for branch elimination
  template<bool Forward>
  MotionResult optimizeImpl(
    const Lines& lines,
    const Position& initialPos,
    const RunningEffort& startingEffort,
    const Position& goalPos,
    std::string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerParams params
  );

  // Templated range implementation - Forward known at compile time
  template<bool Forward>
  RangeMotionResult optimizeToRangeImpl(
    const Lines& lines,
    const Position& initialPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeEnd,
    std::string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerRangeParams params
  );
};
