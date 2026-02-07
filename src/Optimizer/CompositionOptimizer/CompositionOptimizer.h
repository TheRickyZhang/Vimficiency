#pragma once

#include <string_view>
#include <vector>

#include "DiffState.h"
#include "Optimizer/Config.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"
#include "CompositionOptimizerParams.h"
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

  friend std::ostream& operator<<(std::ostream& os, const CompositionResult& cr) {
    os << cr.stats << " goalPos=" << cr.goalPos << "\n";
    for (size_t i = 0; i < cr.results.size(); i++) {
      os << "  [" << i << "] " << cr.results[i] << "\n";
    }
    return os;
  }
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
