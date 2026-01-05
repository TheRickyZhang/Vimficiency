#pragma once

#include <vector>
#include <climits>

#include "Config.h"
#include "Result.h"
#include "Levenshtein.h"
#include "EditBoundary.h"

#include "Utils/Lines.h"
#include "State/EditState.h"

struct EditResult {
  int n;
  int m;
  std::vector<std::vector<Result>> adj;
  EditResult(int n, int m) : n(n), m(m) {
    // Because of SSO this isn't too expensive. We'll see how dense the results generally are later.
    adj = std::vector<std::vector<Result>>(n, std::vector<Result>(m, Result("", INT_MAX)));
  }
};


struct EditOptimizer {
  Config config;

  const int    MAX_SEARCH_DEPTH;
  const double COST_WEIGHT;
  const double USER_EXPLORE_FACTOR; // How much more do we want to explore beyond user's sequence
  const double ABSOLUTE_EXPLORE_FACTOR;
  
  const int F_MOTION_THRESHOLD = 2;
  
  EditOptimizer(const Config& config,
                int max_search_depth = 1e5,
                double cost_weight = 1.0,
                double user_explore_factor = 2.0,
                double absolute_explore_factor = 3.0
                )
  : config(std::move(config)),
    MAX_SEARCH_DEPTH(max_search_depth),
    COST_WEIGHT(cost_weight),
    USER_EXPLORE_FACTOR(user_explore_factor),
    ABSOLUTE_EXPLORE_FACTOR(absolute_explore_factor) {}

  double costToGoal(const Lines& currLines, const Mode& mode, const Levenshtein& editDistanceCalculator) const;

  double heuristic(const EditState& s, const Levenshtein& editDistanceCalculator) const;

  EditResult optimizeEdit(
    const Lines& beginLines,
    const Lines& endLines,
    const EditBoundary& boundary
  );

};
