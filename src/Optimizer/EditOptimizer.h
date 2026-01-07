#pragma once

#include <vector>
#include <climits>
#include <optional>

#include "Config.h"
#include "Result.h"
#include "OptimizerParams.h"
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
  OptimizerParams defaultParams;

  // EditOptimizer-specific: second explore factor for absolute cost bound
  double absoluteExploreFactor = 3.0;

  EditOptimizer(const Config& config, OptimizerParams params = {},
                double absoluteExploreFactor = 3.0)
      : config(std::move(config)),
        defaultParams(params),
        absoluteExploreFactor(absoluteExploreFactor) {}

  double costToGoal(const Lines& currLines, const Mode& mode, const Levenshtein& editDistanceCalculator) const;

  double heuristic(const EditState& s, const Levenshtein& editDistanceCalculator,
                   const OptimizerParams& params) const;

  EditResult optimizeEdit(
    const Lines& beginLines,
    const Lines& endLines,
    const EditBoundary& boundary,
    // Optional search parameter overrides (uses defaultParams if not provided)
    const std::optional<OptimizerParams>& paramsOverride = std::nullopt
  );

};
