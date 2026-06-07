#pragma once

#include <cassert>
#include <string_view>
#include <vector>

#include "CompositionPlan.h"
#include "CompositionOptimizerParams.h"
#include "CompositionSearchContext.h"
#include "EditSequenceSpan.h"
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
struct SearchControl;

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

  // Public construction so the CompositionOptimizer.cpp implementation can
  // build it without coupling every helper through friendship. The contained
  // invariants (one extra fencepost, one TransformResult per diff) are
  // enforced by the asserts in the body.
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

private:
  CompositionSearchStats stats_;
  CompositionPlan plan_;
  std::vector<CompositionExploredState> exploredStates_;
  std::vector<TransformResult> editResults_;
};

// Display-only sidecar produced by `optimizeWithEditSpans()`. Contains the
// usual `CompositionResult` plus, parallel to `result.getResults()`, the
// per-result list of planned-edit byte spans inside that result's flat key
// sequence. Used by `Explore::View` to render Optimal-N header columns
// segmented by composition plan rather than by Vim-token kind.
//
// Invariant (when produced by `optimizeWithEditSpans`):
//   editSpansByResult.size() == result.getResults().size()
//   editSpansByResult[i].size() == result.totalEdits()
//
// We accept the parallel-vector shape here (the codebase usually prefers
// per-edit accessors like `plannedEditAt`) because spans are a result-only
// display side channel, not load-bearing model state. Folding them into
// `Result` would pollute the universal optimizer pipeline used by
// `vf_analyze` and other non-display callers.
struct CompositionTraceResult {
  CompositionResult result;
  std::vector<std::vector<EditSequenceSpan>> editSpansByResult;
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
    const NavContext& navigationContext = NavContext(),

    // Cooperative stop signal; null runs the search to its normal caps.
    const SearchControl* control = nullptr
  );

  // Same as `optimize()`, but additionally records a per-edit byte span for
  // every result the search emits. Intended exclusively for `Explore::View`'s
  // header-row display: `vf_analyze` and other callers must use `optimize()`
  // so they pay zero per-state cost for tracing they don't render.
  // (The trace-aware queue entry uses `[[no_unique_address]]` for its trace
  // field, so the non-traced code path is byte-identical to `optimize()`.)
  CompositionTraceResult optimizeWithEditSpans(
    const Lines& initialLines,
    const CursorPos initialPos,
    const Lines& goalLines,
    const CursorPos goalPos,
    CompositionOptimizerParams params = {},
    std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext(),
    const SearchControl* control = nullptr
  );
};
