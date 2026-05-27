#include "TransformOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(TransformOptimizer_ManualTest, Boundary_VisualDeleteFallbackRespectsBoundary) {
  // Locks the visual-delete fallback path against motion-search boundary
  // drift: when the optimizer emits `v <motion> d` for a constrained slice,
  // the inner motion must respect TransformBoundary (no G/gg/}/H/M/L escaping
  // the slice). Replays each chosen sequence in the FULL buffer (with lines
  // above and below) via the oracle and asserts the surrounding lines are
  // unchanged. Pre-fix, the inner NavOptimizer call ignored the boundary, so
  // a buffer-relative motion picked on the slice could land outside the
  // intended deletion when run on the full buffer.
  // Mid-line-to-mid-line deletion: `dj`/`dk` can't apply (those take full
  // lines), so the visual-delete fallback is competitive with `dNl` /
  // `Nd<motion>`. The full buffer has lines above and below to make absolute
  // motions like G/gg behave differently in slice vs full buffer.
  // Slice has 3 lines so motion G/gg become potentially-cheap targets in
  // the inner nav search. The slice ends mid-line (col 6) on the last
  // effective line so the visual end point isn't at end-of-buffer (forcing
  // a real motion path, not just `G`).
  Lines fullBuffer = {"pre1", "pre2", "aaaaaaaa", "bbbbbbbb", "cccccccc", "post1", "post2"};
  CursorPos initialPos(2, 1), endPos(4, 6);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);

  TransformResult res = pureDeletionResult(editRegion, boundary);

  forEachValidResult(res.getResults(), editRegion, [&](CursorPos slicePos, const auto& seq) {
    // Skip starts on cells that don't exist in the slice (only first-line
    // positions need offsetting; for the rest, slice col == full col).
    int fullCol = slicePos.line == 0 ? slicePos.col + initialPos.col : slicePos.col;
    CursorPos fullPos(slicePos.line + initialPos.line, fullCol);
    SimulationResult nvim = oracle->simulate(
        fullBuffer, fullPos.line, fullPos.col, seq.str());
    // After the slice deletion, the prefix of line "aaaaaaaa" (col 0) joins
    // the suffix of line "cccccccc" (cols 6+); surrounding lines untouched.
    Lines expected{"pre1", "pre2", "acc", "post1", "post2"};
    EXPECT_EQ(nvim.lines, expected)
        << "Sequence '" << seq.str() << "' from full-buffer pos " << fullPos
        << " escaped slice (boundary-violating motion in visual delete?)";
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Sequence '" << seq.str() << "' did not return to Normal mode";
  });

  bool sawVisualDelete = false;
  for (CursorPos slicePos : res.startPositions()) {
    auto bucket = res.resultsAt(slicePos.line, slicePos.col);
    for (size_t i = 0; i < bucket.size(); i++) {
      const auto& seq = bucket[i].getSequence();
      if (seq.empty() || seq.view()[0] != 'v') continue;
      sawVisualDelete = true;

      int fullCol = slicePos.line == 0 ? slicePos.col + initialPos.col : slicePos.col;
      CursorPos fullPos(slicePos.line + initialPos.line, fullCol);
      SimulationResult nvim = oracle->simulate(
          fullBuffer, fullPos.line, fullPos.col, seq.str());
      CursorPos localGoal = res.goalPosAt(slicePos.line, slicePos.col, i);
      CursorPos fullGoal(localGoal.line + initialPos.line, localGoal.col);
      EXPECT_EQ(CursorPos(nvim.row, nvim.col), fullGoal)
          << "Sequence '" << seq.str() << "' recorded the wrong visual-delete cursor";
    }
  }
  EXPECT_TRUE(sawVisualDelete);
}

TEST_F(TransformOptimizer_ManualTest, BackwardWordDeleteFromCol0_ReanchorsToFirstNonBlank) {
  verifySequenceWithOracle(oracle.get(), {"abc", "  def"}, CursorPos(1, 0), "db");
}

}  // namespace
