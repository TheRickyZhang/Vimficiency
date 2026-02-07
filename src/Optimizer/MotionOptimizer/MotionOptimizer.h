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
  // Note: Internally dispatches to optimizeImpl<Forward> based on initialPos vs goalPos
  MotionResult optimize(
    // Core information
    const Lines& lines,
    const Position& initialPos,
    const Position& goalPos,

    // Search tuning (can adjust with designated initializers)
    MotionOptimizerParams params = {},
    const std::string& userSequence = "", // What the user typed, which informs stopping point.

    // Continuation from broader context
    const MotionBoundary& parentBoundary = MotionBoundary(),
    const RunningEffort& startingEffort  = RunningEffort(),

    // Niche settings
    const NavContext& navigationContext = NavContext(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS
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
    const std::string& userSequence = "",

    // Continuation from broader context
    const MotionBoundary& boundary = MotionBoundary(),
    const RunningEffort& startingEffort = RunningEffort(),

    // Niche settings
    const NavContext& navigationContext = NavContext(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS
  );

private:
  // Templated implementation - Forward known at compile time for branch elimination
  template<bool Forward>
  MotionResult optimizeImpl(
    const Lines& lines,
    const Position& initialPos,
    const RunningEffort& startingEffort,
    const Position& goalPos,
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
    const Position& initialPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeEnd,
    const std::string& userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    MotionOptimizerRangeParams params
  );
};
