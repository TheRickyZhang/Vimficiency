#pragma once

#include <bits/stdc++.h>

#include "Optimizer/Config.h"
#include "State/State.h"

struct Result {
  std::string sequence;
  double keyCost;
  Result(std::string s, double c) : sequence(s), keyCost(c) {}
};

struct Optimizer {
  Config config; 
  State startingState;

  static constexpr int RESULT_COUNT = 5;
  static constexpr int MAX_SEARCH_DEPTH = 1e5;
  double costWeight = 1;

  Optimizer(const State& state, const Config& effortModel, int costWeight = 1) 
  : startingState(std::move(state)), config(std::move(effortModel)), costWeight(costWeight)
  {
  }

  double costToGoal(Position p, Position q) const {
    return abs(q.line - p.line) + abs(q.targetCol - p.targetCol);
  }
  double heuristic(const State& s, const Position& goal) const {
    return costWeight * s.effort + costToGoal(s.pos, goal);
  }

  // vector<string> optimizeMovement(const vector<string>& lines, const Position& start, const Position& end);

  std::vector<Result> optimizeMovement(
    const std::vector<std::string>& lines,
    const Position& end,
    const std::string& userSequence
  );
};
