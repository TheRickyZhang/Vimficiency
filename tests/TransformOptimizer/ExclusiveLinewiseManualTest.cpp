#include "TransformOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

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

}  // namespace
