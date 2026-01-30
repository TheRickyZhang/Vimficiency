#pragma once

#include <vector>

#include "Config.h"
#include "Result.h"
#include "RangeResult.h"
#include "OptimizerParams.h"
#include "SearchStats.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "State/RunningEffort.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"

// Backward compatibility alias
using SearchParams = OptimizerParams;

struct MotionResult {
  std::vector<Result> results;
  SearchStats stats;
};

struct MotionOptimizer {
  Config config;

  MotionOptimizer(const Config& config) : config(config) {}

  // For movement only. Builds index for faster movement computation.
  // TODO: Only RunningEffort is continued from pre-existing state, everything else can be fresh. (make sure that we set cost += newCost - previousCost, not cost = newCost!)
  
  // Returns results and search statistics
  // ~ O(n^2)
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
    OptimizerParams params = {}
  );

  // Overload without userSequence - uses unbounded effort exploration
  // Useful when finding optimal path without a user baseline to compare against
  MotionResult optimize(
    const Lines& lines,
    const Position& startPos,
    const Position& endPos,
    const NavContext& navigationContext,
    const MotionBoundary& boundary = MotionBoundary(),
    OptimizerParams params = {}
  );

  // Multi-sink movement optimization: find paths to any position in [rangeFirst, rangeLast]
  // Only RunningEffort maybe continued from previous state.
  // Returns up to params.maxResults unique end positions.
  // - allowMultiplePerPosition=false (default): at most 1 result per end position (best cost)
  // - allowMultiplePerPosition=true: allows multiple results per position (all found paths)
  // Note: resultCount <= range size when allowMultiplePerPosition=false
  // Disables f-motion and count searches for now (need expanded handling)
  std::vector<RangeResult> optimizeToRange(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,  // Continued from caller for correct effort calc
    const Position& rangeFirst,
    const Position& rangeLast,
    const std::string& userSequence,
    NavContext& navigationContext,

    bool allowMultiplePerPosition = false,
    const MotionBoundary& boundary = MotionBoundary(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Search parameters (uses struct defaults if not specified via designated initializers)
    OptimizerParams params = {}
  );
};
