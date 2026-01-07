#pragma once

#include <vector>
#include <string>
#include <optional>

#include "Config.h"
#include "Result.h"
#include "OptimizerParams.h"
#include "EditOptimizer.h"
#include "DiffState.h"
#include "ImpliedExclusions.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "State/CompositionState.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"

struct CompositionOptimizer {
  Config config;
  OptimizerParams defaultParams;

  // CompositionOptimizer-specific parameters
  // Overshooting (going past the next edit region) is penalized more heavily
  // than undershooting (not yet reaching it)
  double overshootPenalty = 3.0;
  // Slight bias towards forward (natural left->right, top->bottom order)
  double forwardBias = 2.0;
  // Max line length for position key encoding
  int maxLineLength = 100;

  CompositionOptimizer(const Config& config, OptimizerParams params = {},
                       double overshootPenalty = 3.0, double forwardBias = 2.0,
                       int maxLineLength = 100)
      : config(std::move(config)),
        defaultParams(params),
        overshootPenalty(overshootPenalty),
        forwardBias(forwardBias),
        maxLineLength(maxLineLength) {}

  // Composes edit transitions + movement. Pre-computes edit regions, then searches for optimal sequence.
  // Much slower; ~ O(n^2) + Sigma (m_i)^3, higher constant factor.
  std::vector<Result> optimize(
    const std::vector<std::string>& startLines,
    const Position startPos,
    const std::vector<std::string>& endLines,
    const Position endPos,
    const std::string& userSequence,

    const NavContext& navigationContext,
    const ImpliedExclusions& impliedExclusions = ImpliedExclusions(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Optional search parameter overrides (uses defaultParams if not provided)
    const std::optional<OptimizerParams>& paramsOverride = std::nullopt
  );

  // Manhattan distance
  double costToGoal(const Position& curr, const Position& goal) const;

  // h(n) for A*: estimates remaining cost
  // Uses suffix sum of min edit costs + distance to next edit region
  double heuristic(const CompositionState& s, int editsCompleted,
                   const std::vector<double>& suffixEditCosts,
                   const std::vector<DiffState>& diffStates,
                   const OptimizerParams& params) const;

  // Compute suffix sums of minimum edit costs
  // suffixEditCosts[i] = sum of min costs for edits i..totalEdits-1
  // suffixEditCosts[totalEdits] = 0
  std::vector<double> computeSuffixEditCosts(const std::vector<EditResult>& editResults) const;

  // Convert buffer position to flat index within edit region's deletedLines
  // Returns -1 if position is not in the edit region
  int bufferPosToEditIndex(const Position& bufferPos, const DiffState& diff) const;

  // Convert flat index within edit region's insertedLines to buffer position
  Position editIndexToBufferPos(int flatIndex, const DiffState& diff) const;

  // Solve each edit region independently
  std::vector<EditResult> calculateEditResults(const std::vector<DiffState>& diffStates);

  // Build intermediate buffer states after each diff
  std::vector<Lines> calculateLinesAfterDiffs(const Lines& startLines, const std::vector<DiffState>& diffStates, int totalEdits);

  // Build position -> editIndex map
  // posToEditIndex[posKey] = list of edit indices that can be started from this position
  // posKey = line * MAX_LINE_LENGTH + col
  // Overlaps possible when edits shrink the buffer
  // Lookup: check if editsCompleted is in posToEditIndex[posKey]
  std::vector<std::vector<int>> buildPosToEditIndex(
      const std::vector<DiffState>& diffStates,
      int maxPosKey
  );

  // Convert Position to posKey
  int posToKey(const Position& pos) const {
    return pos.line * maxLineLength + pos.col;
  }
};
