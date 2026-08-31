// Tests for CompositionResult::plannedEditAt — the per-edit compatibility boundary
// consumed by Explore.

#include <gtest/gtest.h>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
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
    EXPECT_EQ(MyersDiff::applyDiffState(plannedEdit.diff, plannedEdit.preFencepost),
              plannedEdit.postFencepost);

    const auto starts = plannedEdit.transformResult.resultsAt(
        plannedEdit.diff.beginPos.line,
        plannedEdit.diff.beginPos.col);
    EXPECT_FALSE(starts.empty())
        << "expected at least one planned edit start at diff begin for edit "
        << editIndex;
  }
}

TEST(CompositionResultPlannedEditView, ReversedDiffOrderKeepsFencepostsAligned) {
  Config config = Config::uniform();
  CompositionOptimizer opt(config);

  Lines initial = {
      "aaa",
      "middle",
      "tail",
  };
  Lines goal = {
      "a",
      "middle",
      "tail suffix",
  };
  CursorPos initialPos(2, 3);
  CursorPos goalPos = goal.lastPos();
  NavBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  // Reverse-order diff processing is a MyersDiff-only heuristic; VimDiff is
  // forward-only (see CompositionSearchContext.cpp).
  CompositionResult result = opt.optimize(
      initial, initialPos, goal, goalPos,
      CompositionOptimizerParams{}.withMaxResults(1).withDiffAlgorithm(1), "", boundary);

  ASSERT_EQ(result.totalEdits(), 2);
  EXPECT_EQ(result.getPlan().diffAt(0).beginPos.line, 2);
  EXPECT_EQ(result.getPlan().fenceposts.back(), goal);
  for (int editIndex = 0; editIndex < result.totalEdits(); editIndex++) {
    const auto plannedEdit = result.plannedEditAt(editIndex);
    EXPECT_EQ(MyersDiff::applyDiffState(plannedEdit.diff, plannedEdit.preFencepost),
              plannedEdit.postFencepost);
  }
}

TEST(CompositionResultPlannedEditView, ExclusiveDiffEndIsNotAValidTransformStart) {
  Config config = Config::uniform();
  CompositionOptimizer opt(config);

  Lines initial = {
      "keep",
      "delete",
      "next",
  };
  Lines goal = {
      "keep",
      "next",
  };
  NavBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  CompositionResult result = opt.optimize(
      initial, CursorPos(2, 0), goal, goal.lastPos(),
      CompositionOptimizerParams{}.withMaxResults(1), "", boundary);

  ASSERT_EQ(result.totalEdits(), 1);
  const auto plannedEdit = result.plannedEditAt(0);
  ASSERT_EQ(plannedEdit.diff.beginPos, CursorPos(1, 0));
  ASSERT_EQ(plannedEdit.diff.endPos, CursorPos(2, 0));

  EXPECT_FALSE(plannedEdit.transformResult.isValidStartPosition(2, 0));
  EXPECT_TRUE(plannedEdit.transformResult.resultsAt(2, 0).empty());
  EXPECT_FALSE(plannedEdit.transformResult.startPositions().empty());
  for (CursorPos start : plannedEdit.transformResult.startPositions()) {
    EXPECT_NE(start, CursorPos(2, 0));
  }
}

// The plan's diffs are the planner's partition under the search's own cost
// options, untouched by the search. The analyze payload and stored session
// diffs rely on this identity instead of re-running the planner.
TEST(CompositionResultPlannedEditView, DiffsAreThePlannerPartition) {
  Config config = Config::uniform();
  CompositionOptimizer opt(config);

  Lines initial = {
      "alpha beta gamma",
      "delta epsilon",
      "zeta eta theta",
  };
  Lines goal = {
      "alpha BETA gamma",
      "delta epsilon",
      "zeta eta THETA",
  };
  NavBoundary boundary(initial, CursorPos(0, 0), initial.endPos());
  CompositionOptimizerParams params = CompositionOptimizerParams{}
      .withMaxResults(2).withMoveDeleteScale(1.5).withMaxCountRepeat(6);

  CompositionResult result = opt.optimize(
      initial, CursorPos(0, 0), goal, goal.lastPos(), params, "", boundary);

  vector<VimDiff::Plan> plans = VimDiff::calculate(
      initial, goal, config,
      VimDiff::CostOptions{.moveDeleteScale = params.moveDeleteScale,
                           .maxPrefixCount = params.maxPrefixCount});
  ASSERT_FALSE(plans.empty());
  const vector<DiffState>& expected = plans.front().diffs;
  ASSERT_EQ(result.getDiffs().size(), expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    const DiffState& actual = result.getDiffs()[i];
    EXPECT_EQ(actual.beginPos, expected[i].beginPos);
    EXPECT_EQ(actual.endPos, expected[i].endPos);
    EXPECT_EQ(actual.deletedText, expected[i].deletedText);
    EXPECT_EQ(actual.insertedText, expected[i].insertedText);
  }
}

// Over the search's edit cap the result still carries the full plan — stored
// session diffs and the explore view read it — and only the search is declined.
TEST(CompositionResultPlannedEditView, OverEditCapKeepsPlanWithoutResults) {
  Config config = Config::uniform();
  CompositionOptimizer opt(config);

  vector<string> initialText, goalText;
  for (int i = 0; i < 17; i++) {
    initialText.push_back("unchanged prefix text " + to_string(i) + " old");
    goalText.push_back("unchanged prefix text " + to_string(i) + " new");
  }
  Lines initial(initialText);
  Lines goal(goalText);
  NavBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  CompositionResult result = opt.optimize(
      initial, CursorPos(0, 0), goal, goal.lastPos(),
      CompositionOptimizerParams{}.withMaxResults(1), "", boundary);

  ASSERT_GT(result.totalEdits(), 16);
  EXPECT_TRUE(result.getResults().empty());
  EXPECT_EQ(result.getPlan().fenceposts.back(), goal);
}

}  // namespace
