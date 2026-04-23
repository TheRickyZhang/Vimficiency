#pragma once

#include <string_view>
#include <vector>

#include "CompositionPlan.h"
#include "CompositionOptimizerParams.h"
#include "CompositionSearchContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/OptimizerResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/MotionBoundary.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

struct CompositionResult : BaseOptimizerResult<> {
  CompositionResult() = default;

  const CompositionSearchStats& getStats() const { return stats_; }

  const CompositionPlan& getPlan() const { return plan_; }
  const CursorPos& getGoalPos() const { return plan_.finalGoalPos; }
  const std::vector<DiffState>& getDiffs() const { return plan_.diffs; }
  const std::vector<CompositionExploredState>& getExploredStates() const { return exploredStates_; }
  std::vector<CompositionExploredState>& getExploredStates() { return exploredStates_; }
  const std::vector<EditResult>& getEditResults() const { return editResults_; }
  std::vector<EditResult>& getEditResults() { return editResults_; }

  // Prints diff legend with {n} placeholders, then results with typed text substituted.
  friend std::ostream& operator<<(std::ostream& os, const CompositionResult& cr);

private:
  CompositionSearchStats stats_;
  CompositionPlan plan_;
  std::vector<CompositionExploredState> exploredStates_;
  std::vector<EditResult> editResults_;

  friend struct CompositionOptimizer;
  CompositionResult(std::vector<Result> results, CompositionSearchStats stats,
                    CompositionPlan plan,
                    std::vector<CompositionExploredState> exploredStates,
                    std::vector<EditResult> editResults)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      plan_(std::move(plan)),
      exploredStates_(std::move(exploredStates)),
      editResults_(std::move(editResults)) {}
};

struct CompositionOptimizer {
  Config config;

  explicit CompositionOptimizer(const Config& config) : config(config) {}

  // Composes edit transitions + movement. Pre-computes edit regions, then searches for optimal sequence.
  // Much slower; ~ O(n^2) + Sigma (m_i)^3, higher constant factor.
  CompositionResult optimize(
    // Core information
    const Lines& initialLines,
    const CursorPos initialPos,
    const Lines& goalLines,
    const CursorPos goalPos,

    // Search tuning
    CompositionOptimizerParams params = {},
    std::string_view userSequence = "",

    // Continuation from broader context. Note MotionBoundary suffices as full lines are passed in.
    const MotionBoundary& boundary = MotionBoundary(),

    // Niche settings
    const NavContext& navigationContext = NavContext()
  );
};
