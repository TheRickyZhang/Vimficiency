#pragma once

#include <vector>

#include "Optimizer/Config.h"
#include "Optimizer/Result.h"
#include "Optimizer/RangeResult.h"
#include "MotionOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "State/RunningEffort.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"

struct MotionResult {
  std::vector<Result> results;
  SearchStats stats;
};

struct RangeMotionResult {
  std::vector<RangeResult> results;
  SearchStats stats;
};

struct MotionOptimizer {
  Config config;

  MotionOptimizer(const Config& config) : config(config) {}

  // For movement only. Builds index for faster movement computation.
  // TODO: Only RunningEffort is continued from pre-existing state, everything else can be fresh. (make sure that we set cost += newCost - previousCost, not cost = newCost!)

  // Returns results and search statistics
  // ~ O(n^2)
  // Note: Internally dispatches to optimizeImpl<Forward> based on startPos vs endPos
  MotionResult optimize(
    // Core information
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,  // Continued from caller for correct effort calc
    const Position& endPos,
    const std::string& userSequence, // What the user typed, for reference

    // What's necessary for knowing how to apply some motions
    const NavContext& navigationContext,

    // What impacts our universe of exploration options
    const MotionBoundary& boundary = MotionBoundary(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Search parameters (uses struct defaults if not specified via designated initializers)
    MotionOptimizerParams params = {}
  );

  // Overload without userSequence - uses unbounded effort exploration
  // Useful when finding optimal path without a user baseline to compare against
  MotionResult optimize(
    const Lines& lines,
    const Position& startPos,
    const Position& endPos,
    const NavContext& navigationContext,
    const MotionBoundary& boundary = MotionBoundary(),
    MotionOptimizerParams params = {}
  );

  // Multi-sink movement optimization: find paths to any position in [rangeFirst, rangeLast]
  // Only RunningEffort maybe continued from previous state.
  // Returns up to params.maxResults unique end positions (or total paths if allowMultiplePerPosition).
  // Precondition: startPos must NOT be in [rangeFirst, rangeLast] (nothing to optimize)
  // Note: Internally dispatches to optimizeToRangeImpl<Forward> based on startPos vs range
  RangeMotionResult optimizeToRange(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,  // Continued from caller for correct effort calc
    const Position& rangeFirst,
    const Position& rangeLast,
    const std::string& userSequence,
    NavContext& navigationContext,

    const MotionBoundary& boundary = MotionBoundary(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Search parameters - use MotionOptimizerRangeParams for range-specific options
    MotionOptimizerRangeParams params = {}
  );

private:
  // Templated implementation - Forward known at compile time for branch elimination
  template<bool Forward>
  MotionResult optimizeImpl(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& endPos,
    const std::string& userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    MotionOptimizerParams params
  );

  // Templated range implementation - Forward known at compile time
  template<bool Forward>
  RangeMotionResult optimizeToRangeImpl(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeLast,
    const std::string& userSequence,
    NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    MotionOptimizerRangeParams params
  );
};
