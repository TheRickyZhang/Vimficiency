#include "TransformOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(TransformOptimizer_ManualTest, PureDeletion_OracleVerified) {
  // Single test with oracle verification - stress tests cover more shapes
  Lines lines = {"aa", "bb"};
  TransformResult editRes = pureDeletionResult(
      lines,
      TransformBoundary(lines, CursorPos(0, 0), lines.endPos()));
  const auto& res = editRes.getResults();

  EXPECT_TRUE(allPositionsValid(res, lines));

  forEachValidResult(res, lines, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvimRes = verifySequenceWithOracle(oracle.get(), lines, pos, seq.str());
    EXPECT_TRUE(nvimRes.lines.isEmpty() && nvimRes.mode == Mode::Normal)
        << "Sequence '" << seq << "' from " << pos << " did not reach goal";
  });
}

// =============================================================================
// Boundary-Constrained Tests
// =============================================================================

TEST_F(TransformOptimizer_ManualTest, Boundary_LinesBelow) {
  // Edit region has lines below - tests hasLinesBelow constraint
  Lines fullBuffer = {"aa", "bb", "xx"};
  CursorPos initialPos(0, 0), endPos(1, 2);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);

  TransformResult res = pureDeletionResult(editRegion, boundary);
  EXPECT_TRUE(allPositionsValid(res.getResults(), editRegion));
}

TEST_F(TransformOptimizer_ManualTest, Boundary_SingleLineSurrounded) {
  // Single line edit region surrounded by other lines
  // Can't use dd - must use S/cc
  Lines fullBuffer = {"xx", "hello", "xx"};
  CursorPos initialPos(1, 0), endPos(1, 5);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);

  TransformResult res = pureDeletionResult(editRegion, boundary);
  // printResultsDebug(res.typeAllResults, "boundary line surrounded");
  EXPECT_TRUE(allPositionsValid(res.getResults(), editRegion));
}

TEST_F(TransformOptimizer_ManualTest, Boundary_LinewiseCursorContainment) {
  // Verify cursor stays within edit region and surrounding lines unchanged
  Lines fullBuffer = {"xx", "aa", "bb", "yy"};
  CursorPos initialPos(1, 0), endPos(2, 2);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);

  TransformResult res = pureDeletionResult(editRegion, boundary);

  forEachValidResult(res.getResults(), editRegion, [&](CursorPos pos, const auto& seq) {
    // Skip visual mode sequences for now
    if (!seq.empty() && seq.view()[0] == 'v') return;

    CursorPos fullBufferPos(pos.line + initialPos.line, pos.col);
    ApplyResult applied = applySequence(fullBuffer, fullBufferPos, seq.str());

    EXPECT_EQ(applied.lines[0], "xx") << "Line above modified after '" << seq << "'";
    EXPECT_EQ(applied.lines.back(), "yy") << "Line below modified after '" << seq << "'";
    EXPECT_EQ(applied.pos.line, 1)
        << "Cursor escaped edit region! Pos=" << applied.pos << " after '" << seq << "'";
  });
}

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
}

TEST_F(TransformOptimizer_ManualTest, BackwardWordDeleteFromCol0_ReanchorsToFirstNonBlank) {
  verifySequenceWithOracle(oracle.get(), {"abc", "  def"}, CursorPos(1, 0), "db");
}

}  // namespace
