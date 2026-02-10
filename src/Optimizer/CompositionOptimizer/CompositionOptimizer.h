#pragma once

#include <string_view>
#include <vector>

#include "CompositionOptimizerParams.h"
#include "DiffState.h"
#include "Optimizer/Config.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

struct CompositionResult {
  std::vector<Result> results;
  SearchStats stats;
  Position goalPos;  // Cursor position after last edit completes
  std::vector<DiffState> diffs;  // Character-level diff regions used

  const std::vector<Result>& getResults() const { return results; }

  // Prints diff legend with {n} placeholders, then results with typed text substituted.
  friend std::ostream& operator<<(std::ostream& os, const CompositionResult& cr);
};

struct CompositionOptimizer {
  Config config;

  explicit CompositionOptimizer(const Config& config) : config(config) {}

  // Composes edit transitions + movement. Pre-computes edit regions, then searches for optimal sequence.
  // Much slower; ~ O(n^2) + Sigma (m_i)^3, higher constant factor.
  CompositionResult optimize(
    // Core information
    const Lines& initialLines,
    const Position initialPos,
    const Lines& goalLines,
    const Position goalPos,

    // Search tuning
    CompositionOptimizerParams params = {},
    std::string_view userSequence = "",

    // Continuation from broader context. Note MotionBoundary suffices as full lines are passed in.
    const MotionBoundary& boundary = MotionBoundary(),

    // Niche settings
    const NavContext& navigationContext = NavContext()
  );
};
