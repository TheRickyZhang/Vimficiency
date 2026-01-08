#pragma once

#include <vector>
#include <climits>
#include <optional>

#include "Config.h"
#include "Result.h"
#include "OptimizerParams.h"
#include "EditBoundary.h"

#include "Utils/Lines.h"
#include "State/EditState.h"

// DEPRECATED: Old result structure for line-level DP approach.
// Kept temporarily for CompositionOptimizer compatibility.
// Will be removed once CompositionOptimizer is updated to use DeletionResult.
struct EditResult {
  int n;
  int m;
  std::vector<std::vector<Result>> adj;
  EditResult(int n, int m) : n(n), m(m) {
    adj = std::vector<std::vector<Result>>(n, std::vector<Result>(m, Result("", INT_MAX)));
  }
};

// Result of deletion search: for each starting position, optimal sequences to clear buffer
struct DeletionResult {
  // For each linear position index, the best sequence(s) to delete all content
  // Index = row * maxCol + col (linearized position)
  std::vector<Result> results;

  // Dimensions for interpreting indices
  int rows;
  int maxCols;

  DeletionResult(int r, int c) : rows(r), maxCols(c), results(r * c) {}

  Result& at(int row, int col) { return results[row * maxCols + col]; }
  const Result& at(int row, int col) const { return results[row * maxCols + col]; }
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

  // Heuristic for A* search: effort + chars remaining
  double heuristic(const EditState& s, const OptimizerParams& params) const;

  // A* search to find optimal sequences to delete all content from each starting position.
  // Returns best sequence for each (row, col) starting position.
  // Goal state: buffer empty (or single empty line) AND in Insert mode.
  // Boundary constraints:
  // - If hasLinesBelow: can't dd on last line (cursor would escape to content below)
  // - If hasLinesAbove or hasLinesBelow: goal is single empty line (can't delete all lines)
  DeletionResult optimizeDeletion(const Lines& source, const EditBoundary& boundary = EditBoundary{});

  // DEPRECATED: Stub for CompositionOptimizer compatibility.
  // Returns empty result - CompositionOptimizer needs to be updated to use DeletionResult.
  EditResult optimizeEdit(
    const Lines& beginLines,
    const Lines& endLines,
    const EditBoundary& boundary,
    const std::optional<OptimizerParams>& paramsOverride = std::nullopt
  );

};
