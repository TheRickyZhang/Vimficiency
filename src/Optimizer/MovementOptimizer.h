#pragma once

#include <cmath>

#include "Config.h"
#include "Result.h"
#include "ImpliedExclusions.h"
#include "Editor/NavContext.h"
#include "State/MotionState.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"

struct MovementOptimizer {
  Config config;

  const int    MAX_RESULT_COUNT;
  const int    MAX_SEARCH_DEPTH;
  const double COST_WEIGHT;
  const double EXPLORE_FACTOR;

  const int F_MOTION_THRESHOLD = 2;

  MovementOptimizer(const Config& config,
            int max_result_count = 5,
            int max_search_depth = 1e5,
            double cost_weight = 1.0,
            double explore_factor = 2.0)
      : config(std::move(config)),
        MAX_RESULT_COUNT(max_result_count),
        MAX_SEARCH_DEPTH(max_search_depth),
        COST_WEIGHT(cost_weight),
        EXPLORE_FACTOR(explore_factor) {}

  double costToGoal(Position p, Position q) const {
    return abs(q.line - p.line) + abs(q.targetCol - p.targetCol);
  }

  double heuristic(const MotionState& s, const Position& goal) const {
    return COST_WEIGHT * s.getEffort() + costToGoal(s.getPos(), goal);
  }

  // Heuristic for range target: distance to closest point in range
  double heuristicToRange(const MotionState& s, const Position& rangeBegin, const Position& rangeEnd) const {
    Position pos = s.getPos();
    // If in range, distance is 0
    if (pos >= rangeBegin && pos <= rangeEnd) {
      return COST_WEIGHT * s.getEffort();
    }
    // Otherwise, distance to nearest edge
    Position closest = (pos < rangeBegin) ? rangeBegin : rangeEnd;
    return COST_WEIGHT * s.getEffort() + costToGoal(pos, closest);
  }

  // For movement only. Builds index for faster movement computation.
  // ~ O(n^2)
  std::vector<Result> optimize(
    // Core information
    const std::vector<std::string>& lines,
    const MotionState& startingState,
    const Position& endPos,
    const std::string& userSequence, // What the user typed, for reference

    // What's necessary for knowing how to apply some motions
    NavContext& navigationContext,

    // What impacts our universe of exploration options
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS
  );

  // Multi-sink movement optimization: find paths to any position in [rangeBegin, rangeEnd]
  // Returns up to maxResults results, best cost per end position
  // Disables f-motion and count searches for now (need expanded handling)
  std::vector<Result> optimizeToRange(
    const Lines& lines,
    const MotionState& startingState,
    const Position& rangeBegin,
    const Position& rangeEnd,
    const std::string& userSequence,
    NavContext& navigationContext,
    int maxResults,
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS
  );
};
