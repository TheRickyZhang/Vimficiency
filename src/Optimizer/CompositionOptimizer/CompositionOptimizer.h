#pragma once

#include <cassert>
#include <string_view>
#include <vector>

#include "CompositionPlan.h"
#include "CompositionOptimizerParams.h"
#include "CompositionSearchContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/OptimizerResult.h"
#include "Optimizer/Result.h"
#include "Optimizer/SearchStats.h"

#include "Boundary/NavBoundary.h"
#include "Types/NavContext.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

// Non-owning view into one planned edit inside CompositionResult.
// Lifetime: references remain valid only while the parent CompositionResult
// remains alive and unmoved. Consumers should treat this as an ephemeral
// per-edit read view, not something to store long-term.
struct PlannedEditView {
  int editIndex;
  const DiffState& diff;
  const Lines& preFencepost;
  const Lines& postFencepost;
  const TransformResult& transformResult;
};

struct CompositionResult : BaseOptimizerResult<> {
  CompositionResult() = default;

  const CompositionSearchStats& getStats() const { return stats_; }

  // Low-level/raw plan access for optimizer internals, diagnostics, and tests.
  // Explore-style consumers should prefer `plannedEditAt(...)` so the per-edit
  // compatibility boundary is explicit.
  const CompositionPlan& getPlan() const { return plan_; }
  // The user-supplied goal cursor position; the search treats this as the
  // terminal target after all edits have been applied.
  const CursorPos& getGoalPos() const { return plan_.finalGoalPos; }
  // Raw diff list for diagnostics and tests. Consumers that need a specific
  // edit should prefer `plannedEditAt(...)`.
  const std::vector<DiffState>& getDiffs() const { return plan_.diffs; }
  int totalEdits() const { return plan_.totalEdits(); }
  // Explicit compatibility boundary for consumers like Explore: one typed
  // per-edit view instead of reassembling aligned data from parallel vectors.
  [[nodiscard]] PlannedEditView plannedEditAt(int editIndex) const {
    assert(editIndex >= 0 && editIndex < totalEdits() &&
           "CompositionResult::plannedEditAt index out of range");
    return PlannedEditView{
        .editIndex = editIndex,
        .diff = plan_.diffAt(editIndex),
        .preFencepost = plan_.fencepostAt(editIndex),
        .postFencepost = plan_.fencepostAt(editIndex + 1),
        .transformResult = editResults_[static_cast<size_t>(editIndex)],
    };
  }
  const std::vector<CompositionExploredState>& getExploredStates() const { return exploredStates_; }
  std::vector<CompositionExploredState>& getExploredStates() { return exploredStates_; }

  // Prints diff legend with {n} placeholders, then results with typed text substituted.
  friend std::ostream& operator<<(std::ostream& os, const CompositionResult& cr);

private:
  CompositionSearchStats stats_;
  CompositionPlan plan_;
  std::vector<CompositionExploredState> exploredStates_;
  std::vector<TransformResult> editResults_;

  friend struct CompositionOptimizer;
  CompositionResult(std::vector<Result> results, CompositionSearchStats stats,
                    CompositionPlan plan,
                    std::vector<CompositionExploredState> exploredStates,
                    std::vector<TransformResult> transformResults)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      plan_(std::move(plan)),
      exploredStates_(std::move(exploredStates)),
      editResults_(std::move(transformResults)) {
    assert(plan_.fenceposts.size() == plan_.diffs.size() + 1 &&
           "CompositionResult requires one more fencepost than diffs");
    assert(editResults_.size() == plan_.diffs.size() &&
           "CompositionResult requires one TransformResult per diff");
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
    const CursorPos initialPos,
    const Lines& goalLines,
    const CursorPos goalPos,

    // Search tuning
    CompositionOptimizerParams params = {},
    std::string_view userSequence = "",

    // Continuation from broader context. Note NavBoundary suffices as full lines are passed in.
    const NavBoundary& boundary = NavBoundary(),

    // Niche settings
    const NavContext& navigationContext = NavContext()
  );
};
