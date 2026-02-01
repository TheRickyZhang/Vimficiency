#pragma once

#include <vector>

#include "Config.h"
#include "Result.h"
#include "EditOptimizerParams.h"
#include "SearchStats.h"
#include "Boundary/EditBoundary.h"

#include "Utils/Lines.h"

// TODO: Is it helpful to return some sort of vector<Position> to reuse information about how flat indices -> real positions?
struct EditResult {
  // Results that involve deleting everything, and typing the end text
  // [start, end] for each result is [index, sz-1]
  std::vector<Result> typeAllResults;

  // Results that involve replacement. We go in order, so all starts <= first change, end = last change
  // [start, end] for each result is [index, replacementEnd]
  std::vector<Result> replaceResults;

  int replaceEnd = -1;

  // Search statistics for debugging and benchmarking
  SearchStats stats;

  EditResult(int n, std::vector<Result> replaceResults, int replacemeEnd) :
    replaceResults(replaceResults), replaceEnd(replacemeEnd)
  {
    typeAllResults.resize(n);
  }
};

std::ostream& operator<<(std::ostream& os, const EditResult& editResult);


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

  EditOptimizer(const Config& config) : config(std::move(config)) {}

  // find optimal sequences to transform initialLines to goalLines
  // Either delete all initial and type out result, or use replacement
  // Returns results indexed by flattened starting position
  EditResult optimizeEdit(
      const Lines& initialLines,
      const Lines& goalLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {}
  );

  // find optimal sequences to delete all content in initialLines
  // Simpler than optimizeEdit: no typed content, no change conversion
  // Returns EditResult with typeAllResults indexed by flattened starting position
  EditResult optimizePureDeletion(
      const Lines& initialLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {}
  );
};
