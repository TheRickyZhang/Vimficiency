#pragma once

#include <bits/stdc++.h>
#include <cmath>

#include "Config.h"
#include "State/State.h"

struct Result {
  std::string sequence;
  double keyCost;
  Result(std::string s, double c) : sequence(s), keyCost(c) {}
};

struct Optimizer {
  Config config; 
  State startingState;

  const int    MAX_RESULT_COUNT = 5;
  const int    MAX_SEARCH_DEPTH = 1e5;
  const double COST_WEIGHT      = 1;
  const double EXPLORE_FACTOR   = 1.5;

  Optimizer(const State &state, const Config &effortModel,
            int max_result_count = 5,
            int max_search_depth = 1e5,
            double cost_weight = 1.0,
            double explore_factor = 1.5
            )
      : startingState(std::move(state)), config(std::move(effortModel)),
        MAX_RESULT_COUNT(max_result_count),
        MAX_SEARCH_DEPTH(max_search_depth),
        COST_WEIGHT(cost_weight),
        EXPLORE_FACTOR(explore_factor) {}

  double costToGoal(Position p, Position q) const {
    return abs(q.line - p.line) + abs(q.targetCol - p.targetCol);
  }
  double heuristic(const State& s, const Position& goal) const {
    return COST_WEIGHT * s.effort + costToGoal(s.pos, goal);
  }

  // vector<string> optimizeMovement(const vector<string>& lines, const Position& start, const Position& end);

  std::vector<Result> optimizeMovement(
    const std::vector<std::string>& lines,
    const Position& end,
    const std::string& userSequence
  );
};
