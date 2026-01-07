#pragma once

#include <cmath>
#include <optional>

#include "Config.h"
#include "Result.h"
#include "OptimizerParams.h"
#include "ImpliedExclusions.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "State/MotionState.h"
#include "State/RunningEffort.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"

// Backward compatibility alias
using SearchParams = OptimizerParams;

struct MovementOptimizer {
  Config config;
  OptimizerParams defaultParams;

  MovementOptimizer(const Config& config, OptimizerParams params = {})
      : config(config), defaultParams(params) {}

  double costToGoal(Position p, Position q) const {
    return abs(q.line - p.line) + abs(q.targetCol - p.targetCol);
  }

  double heuristic(const MotionState& s, const Position& goal, double costWeight) const {
    return costWeight * s.getEffort() + costToGoal(s.getPos(), goal);
  }

  // Heuristic for range target: distance to closest point in range
  double heuristicToRange(const MotionState& s, const Position& rangeBegin,
                          const Position& rangeEnd, double costWeight) const {
    Position pos = s.getPos();
    // If in range, distance is 0
    if (pos >= rangeBegin && pos <= rangeEnd) {
      return costWeight * s.getEffort();
    }
    // Otherwise, distance to nearest edge
    Position closest = (pos < rangeBegin) ? rangeBegin : rangeEnd;
    return costWeight * s.getEffort() + costToGoal(pos, closest);
  }

  // For movement only. Builds index for faster movement computation.
  // Only RunningEffort is continued from pre-existing state, everything else can be fresh.
  // Returns additional effort and sequence to add to the caller site
  // ~ O(n^2)
  std::vector<Result> optimize(
    // Core information
    const std::vector<std::string>& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,  // Continued from caller for correct effort calc
    const Position& endPos,
    const std::string& userSequence, // What the user typed, for reference

    // What's necessary for knowing how to apply some motions
    const NavContext& navigationContext,

    // What impacts our universe of exploration options
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Optional search parameter overrides (uses defaultParams if not provided)
    const std::optional<OptimizerParams>& paramsOverride = std::nullopt
  );

  // Multi-sink movement optimization: find paths to any position in [rangeBegin, rangeEnd]
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
    const Position& rangeBegin,
    const Position& rangeEnd,
    const std::string& userSequence,
    NavContext& navigationContext,

    bool allowMultiplePerPosition = false,
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Optional search parameter overrides (uses defaultParams if not provided)
    const std::optional<OptimizerParams>& paramsOverride = std::nullopt
  );
};
