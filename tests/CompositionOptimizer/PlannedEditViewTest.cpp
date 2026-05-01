// Tests for CompositionResult::plannedEditAt — the per-edit compatibility boundary
// consumed by Explore.

#include <gtest/gtest.h>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

using namespace std;

namespace {

TEST(CompositionResultPlannedEditView, AlignsDiffFencepostsAndEditResultPerEdit) {
  Config config = Config::uniform();
  CompositionOptimizer opt(config);

  Lines initial = {
      "alpha beta",
      "gamma delta",
  };
  Lines goal = {
      "alpha BETA",
      "gamma DELTA",
  };
  CursorPos initialPos(0, 0);
  CursorPos goalPos = goal.lastPos();
  NavBoundary boundary(initial, initialPos, initial.endPos());

  CompositionResult result = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "", boundary);

  ASSERT_GT(result.totalEdits(), 0);
  const auto& plan = result.getPlan();
  EXPECT_EQ(plan.fenceposts.size(), static_cast<size_t>(result.totalEdits() + 1));
  EXPECT_EQ(plan.diffs.size(), static_cast<size_t>(result.totalEdits()));

  for (int editIndex = 0; editIndex < result.totalEdits(); editIndex++) {
    const auto plannedEdit = result.plannedEditAt(editIndex);

    EXPECT_EQ(plannedEdit.editIndex, editIndex);
    EXPECT_EQ(&plannedEdit.diff, &plan.diffAt(editIndex));
    EXPECT_EQ(plannedEdit.preFencepost, plan.fencepostAt(editIndex));
    EXPECT_EQ(plannedEdit.postFencepost, plan.fencepostAt(editIndex + 1));

    // The bundled post-fencepost must be exactly the diff applied to the
    // bundled pre-fencepost; this is the fencepost contract Explore relies on.
    EXPECT_EQ(Myers::applyDiffState(plannedEdit.diff, plannedEdit.preFencepost),
              plannedEdit.postFencepost);

    const auto starts = plannedEdit.transformResult.resultsAt(
        plannedEdit.diff.beginPos.line,
        plannedEdit.diff.beginPos.col);
    EXPECT_FALSE(starts.empty())
        << "expected at least one planned edit start at diff begin for edit "
        << editIndex;
  }
}

}  // namespace
