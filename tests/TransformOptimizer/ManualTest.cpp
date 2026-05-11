// tests/TransformOptimizer/ManualTest.cpp
//
// Manual tests for TransformOptimizer with hardcoded setups.
// Tests boundary constraints, replacement strategies, and specific behaviors.
// For random/stress tests, see OutputCorrectnessTest.cpp.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TransformOptimizer_ManualTest.*"

#include <climits>
#include <gtest/gtest.h>
#include <memory>

#include "Interpreter/EditInterpreter.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Boundary/TransformBoundary.h"
#include "Types/Lines.h"
// #include "Utils/TestUtils.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class TransformOptimizer_ManualTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  // For correctness on small buffers, set int max upper bound
  TransformOptimizerParams params = TransformOptimizerParams{}.withMaxResults(INT_MAX);
  TransformOptimizer opt{config};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  TransformResult pureDeletionResult(const Lines& initialLines, TransformBoundary boundary) {
    return opt.optimizePureDeletion(initialLines, boundary, params);
  }
};

unique_ptr<NeovimOracle> TransformOptimizer_ManualTest::oracle;

// =============================================================================
// Test Helpers
// =============================================================================

struct ApplyResult {
  Lines lines;
  CursorPos pos;
  Mode mode;

  ApplyResult(Lines lines, CursorPos pos, Mode mode = Mode::Normal)
      : lines(std::move(lines)), pos(pos), mode(mode) {}
};

ApplyResult applySequence(const Lines& source, CursorPos initialPos, const string& sequence) {
  ApplyResult result(source, initialPos);
  string lastEditCmd;
  for (const auto& op : Edit::parseEdits(sequence)) {
    Edit::applyEdit(result.lines, result.pos, result.mode, op, &lastEditCmd);
  }
  return result;
}

bool cursorStateMatches(const ApplyResult& ours, const SimulationResult& nvim) {
  return ours.pos.line == nvim.row && ours.pos.col == nvim.col && ours.mode == nvim.mode;
}

SimulationResult verifySequenceWithOracle(
    NeovimOracle* oracle,
    const Lines& source,
    CursorPos initialPos,
    const string& sequence) {
  SimulationResult nvim = oracle->simulate(source, initialPos.line, initialPos.col, sequence);
  ApplyResult ours = applySequence(source, initialPos, sequence);

  EXPECT_EQ(ours.lines, nvim.lines)
      << "Lines mismatch for seq='" << sequence << "' from " << initialPos << "\n"
      << "  Source: " << source << "\n"
      << "  Ours:   " << ours.lines << "\n"
      << "  Neovim: " << nvim.lines;

  EXPECT_TRUE(cursorStateMatches(ours, nvim))
      << "Cursor mismatch for seq='" << sequence << "'\n"
      << "  Ours:   " << ours.pos << " mode=" << static_cast<int>(ours.mode) << "\n"
      << "  Neovim: (" << nvim.row << "," << nvim.col << ") mode=" << static_cast<int>(nvim.mode);

  return nvim;
}

bool allPositionsValid(const vector<vector<Result>>& results, const Lines& source) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    for (int c = 0; c < source[r].effectiveSize(); c++) {
      if (results[idx++].empty()) {
        return false;
      }
    }
  }
  return true;
}

template<typename Fn>
void forEachValidResult(const vector<vector<Result>>& results, const Lines& lines, Fn fn) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(lines.size()); r++) {
    for (int c = 0; c < lines[r].effectiveSize(); c++) {
      const auto& bucket = results[idx++];
      if (!bucket.empty()) {
        fn(CursorPos(r, c), bucket[0].getSequence());
      }
    }
  }
}

// =============================================================================
// Pure Deletion Tests (full buffer, no boundaries)
// =============================================================================

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


// =============================================================================
// Autoindent-Aware Linewise Change Tests
// =============================================================================
// Linewise changes (cc, cj, ck, {n}cc) pre-fill autoindent from the source line.
// The optimizer must account for this to avoid double-whitespace in typed content.
//
// These tests verify against NeovimOracle only (not applyEdit), since the optimizer
// output includes insert-mode content that Edit::parseEdits doesn't handle.

// Helper: verify all positions produce the expected goal in Neovim
void verifyEditGoal(NeovimOracle* oracle, const Lines& source,
                    const Lines& expectedGoal, const TransformResult& result,
                    int lineOffset = 0) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    for (int c = 0; c < source[r].effectiveSize(); c++) {
      const auto& bucket = result.getResults()[idx++];
      if (bucket.empty()) continue;
      const Result& res = bucket[0];
      CursorPos pos(r + lineOffset, c);
      SimulationResult nvim = oracle->simulate(
          lineOffset == 0 ? source : Lines{}, pos.line, pos.col, res.getSequence().str());
      EXPECT_EQ(nvim.lines, expectedGoal)
          << "Goal mismatch for seq='" << res.getSequence() << "' from " << pos;
      EXPECT_EQ(nvim.mode, Mode::Normal)
          << "Not in normal mode after seq='" << res.getSequence() << "' from " << pos;
    }
  }
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_MatchingIndent) {
  // cj with matching indent: autoindent matches goal, no adjustment needed
  Lines initial = {"    aaa", "    bbb"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_ExcessAutoindent) {
  // Source line has more indent than goal: autoindent 8, goal 4
  Lines initial = {"        aaa", "    bbb"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_InsufficientAutoindent) {
  // Source line has less indent than goal: autoindent 2, goal 8
  Lines initial = {"  aaa", "    bbb"};
  Lines goal = {"        xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_CountedCC) {
  // {n}cc with indent: counted linewise change on indented lines
  Lines initial = {"    aaa", "    bbb", "    ccc"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_WithBoundaryContext) {
  // Linewise change with surrounding lines (hasLinesAbove/Below)
  Lines fullBuffer = {"context_above", "    aaa", "    bbb", "context_below"};
  CursorPos initialPos(1, 0), endPos(2, 7);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);
  Lines goal = {"    xxx"};

  TransformResult res = opt.optimizeTransform(editRegion, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  Lines expectedFull = {"context_above", "    xxx", "context_below"};
  forEachValidResult(res.getResults(), editRegion, [&](CursorPos pos, const auto& seq) {
    CursorPos fullPos(pos.line + initialPos.line, pos.col);
    SimulationResult nvim = oracle->simulate(fullBuffer, fullPos.line, fullPos.col, seq.str());
    EXPECT_EQ(nvim.lines, expectedFull)
        << "Goal mismatch for seq='" << seq << "' from " << fullPos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << fullPos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_NoIndent) {
  // Linewise change on unindented lines with indented goal
  Lines initial = {"aaa", "bbb"};
  Lines goal = {"    xxx"};
  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult res = opt.optimizeTransform(initial, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  forEachValidResult(res.getResults(), initial, [&](CursorPos pos, const auto& seq) {
    SimulationResult nvim = oracle->simulate(initial, pos.line, pos.col, seq.str());
    EXPECT_EQ(nvim.lines, goal)
        << "Goal mismatch for seq='" << seq << "' from " << pos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << pos;
  });
}

TEST_F(TransformOptimizer_ManualTest, AutoindentLinewise_CollapseWithBS) {
  // Exercises the collapse path where cursorLine > 0 (BS in collapse).
  // ck from line 2 would change lines [1,2], beginLine=1, needing BS to join
  // with prefix line above. Autoindent from indented source line.
  Lines fullBuffer = {"prefix", "    aaa", "    bbb", "suffix"};
  CursorPos initialPos(1, 0), endPos(2, 7);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  TransformBoundary boundary(fullBuffer, initialPos, endPos);
  Lines goal = {"    xxx"};

  TransformResult res = opt.optimizeTransform(editRegion, goal, boundary, params);
  ASSERT_FALSE(res.getResults()[0].empty());

  Lines expectedFull = {"prefix", "    xxx", "suffix"};
  forEachValidResult(res.getResults(), editRegion, [&](CursorPos pos, const auto& seq) {
    CursorPos fullPos(pos.line + initialPos.line, pos.col);
    SimulationResult nvim = oracle->simulate(fullBuffer, fullPos.line, fullPos.col, seq.str());
    EXPECT_EQ(nvim.lines, expectedFull)
        << "Goal mismatch for seq='" << seq << "' from " << fullPos;
    EXPECT_EQ(nvim.mode, Mode::Normal)
        << "Not in normal mode after seq='" << seq << "' from " << fullPos;
  });
}

// =============================================================================
// Sentence motion regression: d) with cross-line sentence boundary
// =============================================================================

TEST_F(TransformOptimizer_ManualTest, SentenceDeleteCrossLine_OracleVerified) {
  // d) where the next sentence starts at col 0 of the next line.
  // The half-open range must span the line boundary: [cursor, nextLineCol0).
  // Regression: getPrevPos(CursorPos(1,0)) collapses to end-of-line-0,
  // producing a single-line range that fails to delete the newline.
  Lines lines = {"End.", "Start"};
  verifySequenceWithOracle(oracle.get(), lines, CursorPos(0, 0), "d)");
}

// =============================================================================
// Exclusive-linewise adjustment: d)/d(/d}/d{  (see :help exclusive-linewise)
// =============================================================================
//
// Vim adjusts exclusive motions when the half-open range end is at col 0 of
// another line.  The "end" is the exclusive endpoint of the range:
//   - Forward motions (d), d}):  end = motion destination
//   - Backward motions (d(, d{): end = cursor position (the higher end)
//
// Two rules apply:
//   Linewise:  begin.col == 0 AND end.col == 0  → linewise delete
//   BackedUp:  begin.col > 0  AND end.col == 0  → end backs up to end of
//              previous line; newline is preserved
//
// Shared utility: VimCore::resolveExclusiveDeleteRange() in VimEditUtils.h
// These tests verify that applyEdit matches Neovim for each case.

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_ForwardSentence_Linewise) {
  // d) from col 0: both positions at col 0 → linewise
  verifySequenceWithOracle(oracle.get(), {"End.", "Start"}, {0, 0}, "d)");
  verifySequenceWithOracle(oracle.get(), {"Hello.", "World"}, {0, 0}, "d)");
  verifySequenceWithOracle(oracle.get(), {".", "b"}, {0, 0}, "d)");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_ForwardSentence_BackedUp) {
  // d) from col > 0: endpoint at col 0 → back up (newline preserved)
  verifySequenceWithOracle(oracle.get(), {"be.df.", ".ee  cb"}, {0, 2}, "d)");
  verifySequenceWithOracle(oracle.get(), {"ab.cd.", "ef"}, {0, 2}, "d)");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_BackwardSentence_Linewise) {
  // d( from col 0: both positions at col 0 → linewise
  verifySequenceWithOracle(oracle.get(), {"End.", "Start"}, {1, 0}, "d(");
  verifySequenceWithOracle(oracle.get(), {"Hello.", "World"}, {1, 0}, "d(");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_BackwardSentence_BackedUp) {
  // d( from col 0 where endpoint is NOT at col 0 → back up cursor end
  verifySequenceWithOracle(oracle.get(), {"End. xyz", "abc"}, {1, 0}, "d(");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_ForwardParagraph_Linewise) {
  // d} from col 0: linewise
  verifySequenceWithOracle(oracle.get(), {"abc", "", "def"}, {0, 0}, "d}");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_ForwardParagraph_BackedUp) {
  // d} from col > 0: backed up
  verifySequenceWithOracle(oracle.get(), {"abc", "", "def"}, {0, 2}, "d}");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_BackwardParagraph_Linewise) {
  // d{ from col 0: linewise
  verifySequenceWithOracle(oracle.get(), {"abc", "", "def"}, {2, 0}, "d{");
  verifySequenceWithOracle(oracle.get(), {"        aaa", "    bbb"}, {1, 0}, "d{");
}

TEST_F(TransformOptimizer_ManualTest, ExclusiveLineAdjust_BackwardParagraph_BackedUp) {
  // d{ from col > 0: backed up
  verifySequenceWithOracle(oracle.get(), {"abc", "", "def"}, {2, 2}, "d{");
}

// =============================================================================
// Note: Stress tests (random buffers) are in OutputCorrectnessTest.cpp
// =============================================================================
