#pragma once

#include <vector>
#include <optional>

#include "Config.h"
#include "Result.h"
#include "OptimizerParams.h"
#include "Boundary/EditBoundary.h"

#include "Utils/Lines.h"

struct EditResult {
  // Results that involve deleting everything, and typing the end text
  // start = index, end = sz-1
  std::vector<Result> typeAllResults;

  // Results that involve replacement. Start indices only go until first replace region, and end is always the last replace pos.
  // start = index, end = replacementEnd
  std::vector<Result> replacementResults;

  int replacementEnd = -1;

  EditResult(int n, int m, int x) {
    typeAllResults.resize(n);
    replacementResults.resize(m);
    replacementEnd = x;
  }
};


// Try to find an optimal replacement sequence for same-length transformations.
// Populates res with results for each starting position (0 to firstDiff).
//
// @param deleted            The characters being removed (no newlines)
// @param inserted           The characters being added (must be same length as deleted)
// @param config             Keyboard config for cost calculation
// @param lastReplacementPos Output: column of last replacement (for cursor tracking)
// @param res                Output: results indexed by starting position
void tryReplacement(const std::string& deleted,
                    const std::string& inserted,
                    const Config& config,
                    int& lastReplacementPos,
                    std::vector<Result>& res);


struct EditOptimizer {
  Config config;
  OptimizerParams defaultParams;

  EditOptimizer(const Config& config, OptimizerParams params = {})
      : config(std::move(config)),
        defaultParams(params) {}

  // Heuristic for A* search: effort + chars remaining
  double heuristic(const Lines& lines) const;

  // Main entry point: find optimal sequences to transform startLines to endLines
  // within the given edit boundary constraints.
  //
  // Returns results indexed by starting position (flat index, not including newlines).
  EditResult optimizeEdit(
      const Lines& startLines,
      const Lines& endLines,
      EditBoundary editBoundary,
      const std::optional<OptimizerParams>& paramsOverride = std::nullopt
  );
};
