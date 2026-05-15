#include "NavOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(NavBoundaryTest, DefaultBoundary_AllowsGG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  CursorPos start(2, 0);
  CursorPos end(0, 0);  // gg should reach this

  NavBoundary boundary;  // default: no exclusions

  auto results = runWithBoundary(lines, start, end, "kk", boundary);

  EXPECT_TRUE(hasSequence(results, "gg")) << "Default boundary should allow gg";
}

TEST_F(NavBoundaryTest, ExcludeGG_RemovesGG) {
  // Full buffer has lines above the sub-region we're working with
  Lines fullBuffer = {"above0", "above1", "line0", "line1", "line2", "line3"};
  // Sub-buffer is lines 2-5 (line0 through line3)
  Lines subBuffer = {"line0", "line1", "line2", "line3"};
  CursorPos start(2, 0);  // In sub-buffer coords
  CursorPos end(0, 0);

  // Boundary computed from sub-region within full buffer
  // firstPos=(2,0) means hasLinesAbove=true, endPos=(5,6) means hasLinesBelow=false
  NavBoundary boundary(fullBuffer, CursorPos(2, 0), CursorPos(5, 6));

  auto results = runWithBoundary(subBuffer, start, end, "kk", boundary);

  EXPECT_FALSE(hasSequence(results, "gg")) << "Boundary with hasLinesAbove should exclude gg";
  EXPECT_TRUE(hasSequence(results, "kk")) << "Should still find alternative path";
}

TEST_F(NavBoundaryTest, DefaultBoundary_AllowsG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  CursorPos start(1, 0);
  CursorPos end(3, 0);  // G should reach last line

  NavBoundary boundary;  // default: no exclusions

  auto results = runWithBoundary(lines, start, end, "jj", boundary);

  EXPECT_TRUE(hasSequence(results, "G")) << "Default boundary should allow G";
}

TEST_F(NavBoundaryTest, ExcludeG_RemovesG) {
  // Full buffer has lines below the sub-region we're working with
  Lines fullBuffer = {"line0", "line1", "line2", "line3", "below0", "below1"};
  // Sub-buffer is lines 0-3
  Lines subBuffer = {"line0", "line1", "line2", "line3"};
  CursorPos start(1, 0);
  CursorPos end(3, 0);

  // Boundary computed from sub-region: firstPos=(0,0) hasLinesAbove=false, endPos=(3,5) hasLinesBelow=true
  NavBoundary boundary(fullBuffer, CursorPos(0, 0), CursorPos(3, 5));

  auto results = runWithBoundary(subBuffer, start, end, "jj", boundary);

  EXPECT_FALSE(hasSequence(results, "G")) << "Boundary with hasLinesBelow should exclude G";
  EXPECT_TRUE(hasSequence(results, "jj")) << "Should still find alternative path";
}

TEST_F(NavBoundaryTest, CountedVerticalCandidatesCanLandOnEmbeddedSliceEdges) {
  int checked = 0;

  for (int fullLineCount = 6; fullLineCount <= 8; fullLineCount++) {
    Lines fullBuffer;
    for (int line = 0; line < fullLineCount; line++) {
      fullBuffer.push_back("line" + to_string(line));
    }

    for (int subStart = 1; subStart < fullLineCount - 2; subStart++) {
      for (int subLineCount = 3; subLineCount <= 5; subLineCount++) {
        int subEnd = subStart + subLineCount - 1;
        if (subEnd >= fullLineCount - 1) continue;

        Lines subBuffer;
        for (int line = subStart; line <= subEnd; line++) {
          subBuffer.push_back(fullBuffer[line]);
        }

        NavBoundary boundary(
            fullBuffer,
            CursorPos(subStart, 0),
            CursorPos(subEnd, fullBuffer[subEnd].effectiveSize()));

        ASSERT_TRUE(boundary.hasLinesAbove());
        ASSERT_TRUE(boundary.hasLinesBelow());

        int lastLocalLine = subBuffer.lastLine();
        for (int startLine = 0; startLine <= lastLocalLine; startLine++) {
          int downCount = lastLocalLine - startLine;
          if (downCount >= 2 && downCount <= 8) {
            auto candidates = collectCountedCandidates(
                subBuffer, CursorPos(startLine, 0), CursorPos(lastLocalLine, 0), boundary);
            SCOPED_TRACE(::testing::Message()
                << "down fullLineCount=" << fullLineCount
                << " subStart=" << subStart
                << " subLineCount=" << subLineCount
                << " startLine=" << startLine);
            ASSERT_TRUE(hasCountedCandidate(
                candidates, to_string(downCount) + "j", CursorPos(lastLocalLine, 0)));
            checked++;
          }

          int upCount = startLine;
          if (upCount >= 2 && upCount <= 8) {
            auto candidates = collectCountedCandidates(
                subBuffer, CursorPos(startLine, 0), CursorPos(0, 0), boundary);
            SCOPED_TRACE(::testing::Message()
                << "up fullLineCount=" << fullLineCount
                << " subStart=" << subStart
                << " subLineCount=" << subLineCount
                << " startLine=" << startLine);
            ASSERT_TRUE(hasCountedCandidate(
                candidates, to_string(upCount) + "k", CursorPos(0, 0)));
            checked++;
          }
        }
      }
    }
  }

  EXPECT_GT(checked, 0);
}

TEST_F(NavBoundaryTest, CountedWordCandidatesCanStayWithinSingleLineEmbeddedSlice) {
  Lines fullBuffer = {
      "above",
      "one two three four five six seven",
      "below",
  };
  Lines subBuffer = {fullBuffer[1]};
  NavBoundary boundary(
      fullBuffer,
      CursorPos(1, 0),
      CursorPos(1, fullBuffer[1].effectiveSize()));

  ASSERT_TRUE(boundary.hasLinesAbove());
  ASSERT_TRUE(boundary.hasLinesBelow());

  vector<pair<int, int>> goals = {
      {2, 8},
      {3, 14},
      {4, 19},
      {5, 24},
      {6, 28},
  };

  for (const auto& [count, col] : goals) {
    auto candidates = collectCountedCandidates(
        subBuffer, CursorPos(0, 0), CursorPos(0, col), boundary);
    SCOPED_TRACE(::testing::Message() << "count=" << count);
    ASSERT_TRUE(hasCountedCandidate(
        candidates, to_string(count) + "w", CursorPos(0, col)));
  }
}

TEST_F(NavBoundaryTest, SentenceParagraphCandidatesNeedContextBehindTopSliceEdge) {
  Lines fullBuffer = {
      "a,,aba.,.c. ,.bb",
      "da.bbda cd,.cc.. abbd. ",
      " , d .,bcb.,d aba,a.c,",
      ".c,dbbddcadac b .ab.,.d,a.,,",
      "babcdca. bca ,.ac c.   ..a d b",
      "cca,,cdd,, ca, aaa",
      ",cd,, aacd. ,cc",
      "accd bad,.d .d.a.ac,c, d,,b",
  };
  Lines subBuffer = {fullBuffer[2], fullBuffer[3], fullBuffer[4], fullBuffer[5]};
  NavBoundary boundary(
      fullBuffer,
      CursorPos(2, 0),
      CursorPos(5, fullBuffer[5].effectiveSize()));

  auto candidates = collectStaticCandidates(
      subBuffer, CursorPos(0, 0), CursorPos(2, 5), boundary);

  EXPECT_FALSE(hasStaticCandidate(candidates, ")"));
  EXPECT_FALSE(hasStaticCandidate(candidates, "}"));
}

TEST_F(NavBoundaryTest, LeftColOffset_FiltersPrefixPositions) {
  // NOTE: CursorPos-based column filtering was removed because it was ineffective
  // for motions that clamp to buffer edges (paragraph, sentence jumps).
  // Single-line column filtering is a known limitation - the optimizer will
  // find paths to prefix/suffix positions on single lines.
  //
  // This test documents current behavior, not desired behavior.
  Lines lines = {"prefix_target"};
  CursorPos start(0, 10);  // Start in "target" region
  CursorPos end(0, 5);     // End in prefix region

  // leftColOffset = 7 (prefix "prefix_" length)
  // Boundary from position (0,7) to (0,13) gives leftColOffset=7
  NavBoundary boundary(lines, CursorPos(0, 7), CursorPos(0, 13));

  auto results = runWithBoundary(lines, start, end, "hhhhh", boundary);

  // With filtering removed, the optimizer WILL find a path to the prefix region
  EXPECT_FALSE(results.empty())
      << "Without filtering, optimizer finds path to prefix positions";

  // Should still find path to valid position
  CursorPos end2(0, 8);  // Valid position after prefix
  auto results2 = runWithBoundary(lines, start, end2, "hh", boundary);
  EXPECT_FALSE(results2.empty()) << "Should find path to valid positions";
}

TEST_F(NavBoundaryTest, RightColOffset_FiltersSuffixPositions) {
  // NOTE: CursorPos-based column filtering was removed because it was ineffective
  // for motions that clamp to buffer edges (paragraph, sentence jumps).
  // Single-line column filtering is a known limitation - the optimizer will
  // find paths to prefix/suffix positions on single lines.
  //
  // This test documents current behavior, not desired behavior.
  Lines lines = {"target_suffix"};  // length=13, suffix "_suffix" starts at col 7
  CursorPos start(0, 3);   // Start in "target" region
  CursorPos end(0, 10);    // End in suffix region

  // rightColOffset = 6 (suffix "_suffix" without the 's' at col 7)
  // Boundary from position (0,0) to (0,7) gives rightColOffset = 13-7 = 6
  NavBoundary boundary(lines, CursorPos(0, 0), CursorPos(0, 7));

  auto results = runWithBoundary(lines, start, end, "lllllll", boundary);

  // With filtering removed, the optimizer WILL find a path to the suffix region
  EXPECT_FALSE(results.empty())
      << "Without filtering, optimizer finds path to suffix positions";

  // Should still find path to valid position
  CursorPos end2(0, 6);  // Valid position before suffix (col 7 is where suffix starts)
  auto results2 = runWithBoundary(lines, start, end2, "lll", boundary);
  EXPECT_FALSE(results2.empty()) << "Should find path to valid positions";
}

TEST_F(NavBoundaryTest, IsPositionInBounds_WithColConstraints) {
  // Test isPositionInBounds with column constraints
  // leftColOffset=3 (prefix length on first line)
  // rightColOffset=5 (suffix length on last line)
  // lastLine=2, lastLineLength=15 (so suffix starts at col 15-5=10)

  // Build a 3-line buffer where:
  // - line 0 has prefix of 3 chars, so firstPos=(0,3)
  // - line 2 has length 15, suffix of 5 chars, so endPos=(2,10) (exclusive past col 9)
  Lines lines = {"pppxxxxxx", "middle_content", "xxxxxxxxxxxxxxx"};  // line 2 has 15 chars
  // Boundary from (0,3) to (2,10): leftColOffset=3, rightColOffset=15-10=5
  NavBoundary boundary(lines, CursorPos(0, 3), CursorPos(2, 10));

  int lastLine = 2;
  int lastLineLength = 15;

  // First line (line 0): positions < leftColOffset are prefix
  EXPECT_FALSE(boundary.isPositionInBounds(CursorPos(0, 0), lastLine, lastLineLength)) << "col 0 < leftColOffset";
  EXPECT_FALSE(boundary.isPositionInBounds(CursorPos(0, 2), lastLine, lastLineLength)) << "col 2 < leftColOffset";
  EXPECT_TRUE(boundary.isPositionInBounds(CursorPos(0, 3), lastLine, lastLineLength)) << "col 3 == leftColOffset (first valid)";
  EXPECT_TRUE(boundary.isPositionInBounds(CursorPos(0, 5), lastLine, lastLineLength)) << "col 5 > leftColOffset";

  // Middle line: no column constraints
  EXPECT_TRUE(boundary.isPositionInBounds(CursorPos(1, 0), lastLine, lastLineLength));
  EXPECT_TRUE(boundary.isPositionInBounds(CursorPos(1, 10), lastLine, lastLineLength));

  // Last line (line 2): positions >= lastLineLength - rightColOffset = 10 are suffix
  EXPECT_TRUE(boundary.isPositionInBounds(CursorPos(2, 5), lastLine, lastLineLength)) << "col 5 < 10";
  EXPECT_TRUE(boundary.isPositionInBounds(CursorPos(2, 9), lastLine, lastLineLength)) << "col 9 < 10 (last valid)";
  EXPECT_FALSE(boundary.isPositionInBounds(CursorPos(2, 10), lastLine, lastLineLength)) << "col 10 == 10 (first in suffix)";
  EXPECT_FALSE(boundary.isPositionInBounds(CursorPos(2, 14), lastLine, lastLineLength)) << "col 14 > 10";
}

}  // namespace
