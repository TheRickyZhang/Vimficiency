#pragma once

#include <cmath>

#include "Config.h"
#include "Result.h"
#include "ImpliedExclusions.h"
#include "Editor/NavContext.h"
#include "State/State.h"
#include "Keyboard/MotionToKeys.h"
#include "Keyboard/EditToKeys.h"

// Should this be placed here?

std::ostream& operator<<(std::ostream& os, Result r);

struct Optimizer {
  Config config; 

  const int    MAX_RESULT_COUNT;
  const int    MAX_SEARCH_DEPTH;
  const double COST_WEIGHT;
  const double EXPLORE_FACTOR;
  
  const int F_MOTION_THRESHOLD = 2;
  const int MAX_LINE_LENGTH = 100;

  Optimizer(const Config& config,
            int max_result_count = 5,
            int max_search_depth = 1e5,
            double cost_weight = 1.0,
            double explore_factor = 2.0
            )
      : config(std::move(config)),
        MAX_RESULT_COUNT(max_result_count),
        MAX_SEARCH_DEPTH(max_search_depth),
        COST_WEIGHT(cost_weight),
        EXPLORE_FACTOR(explore_factor) {}

  double costToGoal(Position p, Position q) const {
    return abs(q.line - p.line) + abs(q.targetCol - p.targetCol);
  }
  double heuristic(const State& s, const Position& goal) const {
    return COST_WEIGHT * s.getEffort() + costToGoal(s.getPos(), goal);
  }


  // n = characters in lines, m = total edit boundary length, m_i = length of edit i.

  // For movement only. Builds index for faster movement computation.
  // ~ O(n^2)
  std::vector<Result> optimizeMovement(
    // Core information
    const std::vector<std::string>& lines,
    const State& startingState,
    const Position& endPos,
    const std::string& userSequence, // What the user typed, for reference

    // What's necessary for knowing how to apply some motions
    NavContext& navigationContext,

    // What impacts our universe of exploration options
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS
  );

  // If start/end lines differ, pre-compute change transitions, then do movement search with them
  // Much slower; ~ O(n^2) + Σ (m_i)^3, higher constant factor.
  std::vector<Result> optimizeChanges(
    const std::vector<std::string>& startLines,
    const Position startPos,
    const std::vector<std::string>& endLines,
    const Position endPos,

    const NavContext& navigationContext,
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,
    const EditToKeys& editToKeys = EDIT_EXPLORABLE_MOTIONS
  );
};
