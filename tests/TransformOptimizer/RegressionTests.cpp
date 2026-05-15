// tests/TransformOptimizer/RegressionTests.cpp
//
// Regression tests for TransformOptimizer edge cases.
// Run: ./build/tests/vimficiency_tests --gtest_filter="TransformOptimizerRegression.*"

#include <cassert>

#include <gtest/gtest.h>

#include <vector>

#include "Boundary/TransformBoundary.h"
#include "Effort/EffortBank.h"
#include "Interpreter/EditInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformExplorer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Types/Lines.h"

using namespace std;

namespace {

struct EmbeddedCase {
  Lines fullBuffer;
  Lines editRegion;
  Lines goalLines;
  TransformBoundary boundary;
  CursorPos firstPos;
  CursorPos endPos;  // exclusive
};

EmbeddedCase buildSmallEmbeddedCaseSeed465950() {
  // Reproduces the benchmark shape from EditOpt/SmallEmbeddedChange/3.
  RandomGen::seed(465950);

  int numLines = 3;
  Lines fullBuffer = randomLines(numLines + 1, 8, 15);

  int prefixLen = min(4, static_cast<int>(fullBuffer[0].size()) / 2);
  int lastLine = static_cast<int>(fullBuffer.size()) - 1;
  int suffixLen = min(4, static_cast<int>(fullBuffer[lastLine].size()) / 2);

  CursorPos firstPos(0, prefixLen);
  CursorPos endPos(lastLine, static_cast<int>(fullBuffer[lastLine].size()) - suffixLen);
  if (endPos.col <= 0) endPos.col = static_cast<int>(fullBuffer[lastLine].size());

  Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
  TransformBoundary boundary(fullBuffer, firstPos, endPos);
  Lines goalLines = randomLines(static_cast<int>(editRegion.size()), 4, 8);

  return {fullBuffer, editRegion, goalLines, boundary, firstPos, endPos};
}

CursorPos toFullBufferPos(CursorPos localPos, CursorPos regionBegin) {
  localPos.line += regionBegin.line;
  if (localPos.line == regionBegin.line) {
    localPos.col += regionBegin.col;
  }
  return localPos;
}

}  // namespace

TEST(TransformOptimizerRegression, BoundaryAwareReplayPrefixKeepsXApplicable) {
  EmbeddedCase test = buildSmallEmbeddedCaseSeed465950();

  // For this benchmark-derived shape, effective lines equal fullBuffer.
  Lines replayLines = test.editRegion;
  replayLines[0] = test.boundary.prefix() + replayLines[0];
  replayLines[replayLines.lastLine()] += test.boundary.suffix();
  ASSERT_EQ(replayLines, test.fullBuffer);

  int leftOffset = test.boundary.leftColOffset();
  ASSERT_GT(static_cast<int>(replayLines[0].size()), leftOffset + 1);

  CursorPos pos(0, leftOffset + 1);  // startIndex=1 in effective coordinates
  Mode mode = Mode::Normal;
  string lastEditCmd;

  // Use a sequence that exercises boundary-aware prefix replay and leaves
  // `X` applicable afterward.
  auto parsedPrefix = Edit::parseEdits("DdaW");
  assert(parsedPrefix);
  for (const ParsedEdit& op : *parsedPrefix) {
    Edit::applyEdit(replayLines, pos, mode, op, &lastEditCmd,
                    test.boundary.hasLinesBelow(),
                    test.boundary.leftColOffset(),
                    test.boundary.rightColOffset(),
                    test.boundary.hasLinesAbove());
  }

  EXPECT_EQ(mode, Mode::Normal);
  EXPECT_EQ(lastEditCmd, "daW");
  EXPECT_GT(pos.col, 0) << "Prefix replay should leave room for following X";

  const ParsedEdit x("X");
  Edit::applyEdit(replayLines, pos, mode, x, &lastEditCmd,
                  test.boundary.hasLinesBelow(),
                  test.boundary.leftColOffset(),
                  test.boundary.rightColOffset(),
                  test.boundary.hasLinesAbove());
  EXPECT_EQ(mode, Mode::Normal);
}

TEST(TransformOptimizerRegression, CountedDwWithLinesBelowBoundaryMatchesLocalSemantics) {
  const ParsedEdit fourDw("dw", 4);

  Lines withBoundary = {" ecbb.abec"};
  CursorPos posWithBoundary(0, 0);
  Mode modeWithBoundary = Mode::Normal;
  string lastEditWithBoundary;
  Edit::applyEdit(withBoundary, posWithBoundary, modeWithBoundary, fourDw,
                  &lastEditWithBoundary,
                  /*hasLinesBelow=*/true,
                  /*leftColOffset=*/0,
                  /*rightColOffset=*/0,
                  /*hasLinesAbove=*/false);

  Lines localOnly = {" ecbb.abec"};
  CursorPos posLocal(0, 0);
  Mode modeLocal = Mode::Normal;
  string lastEditLocal;
  Edit::applyEdit(localOnly, posLocal, modeLocal, fourDw,
                  &lastEditLocal,
                  /*hasLinesBelow=*/false,
                  /*leftColOffset=*/0,
                  /*rightColOffset=*/0,
                  /*hasLinesAbove=*/false);

  EXPECT_EQ(withBoundary, localOnly);
  EXPECT_EQ(posWithBoundary, posLocal);
  EXPECT_EQ(modeWithBoundary, modeLocal);
}

TEST(TransformOptimizerRegression, BackwardParagraphExplorerEmitsDLeftBrace) {
  Config config = Config::uniform();
  Lines lines = {"abc", "", "def"};
  TransformBoundary boundary(lines, CursorPos(0, 0), lines.endPos());
  TransformOptimizerParams params;
  EffortBank bank(config);
  TransformExplorer explorer(
      boundary, params, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawLinewiseDLeftBrace = false;
  bool sawUnexpectedCharwiseDLeftBrace = false;

  explorer.exploreParagraphEdits<false>(
      Edit::BACKWARD_PARAGRAPH_EDITS, CursorPos(2, 0), lines,
      [&](const auto&, const SequenceBinding& cmd) {
        if (cmd.base.seq.view() == "d{") {
          sawUnexpectedCharwiseDLeftBrace = true;
        }
      },
      [&](LineRange range, const SequenceBinding& cmd) {
        if (cmd.base.seq.view() == "d{" &&
            range.beginLine == 1 && range.endLine == 2) {
          sawLinewiseDLeftBrace = true;
        }
      });

  EXPECT_TRUE(sawLinewiseDLeftBrace);
  EXPECT_FALSE(sawUnexpectedCharwiseDLeftBrace);
}

TEST(TransformOptimizerRegression, SuffixCacheExpandsMismatchedDotRepeatContext) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {"ce.d.d ", "aa.eb", "f. e", ".e ae"};
  const Lines editRegion = {".d.d ", "aa.eb", "f. e", ".e a"};
  const Lines goalLines = {"fbbccbae", "c.dead", "aaadd   ", "e.b,"};
  const Lines expectedFull = {
      "cefbbccbae",
      "c.dead",
      "aaadd   ",
      "e.b,e",
  };
  TransformBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(3, 4));

  TransformResult result = optimizer.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}
          .withMaxResults(200)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(2, 2);
  ASSERT_FALSE(bucket.empty());

  size_t checked = min<size_t>(3, bucket.size());
  for (size_t i = 0; i < checked; i++) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(2, 2),
        bucket[i].getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "transform suffix cache dot context"))
        << "sequence=" << bucket[i].getSequence().str();
  }
}

TEST(TransformOptimizerRegression, StraddledPureDeletionAllResultsReplayToGoal) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  struct Case {
    CursorPos begin;
    CursorPos end;
    Lines goal;
  };

  const Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  const vector<Case> cases = {
    {CursorPos(0, 5), CursorPos(1, 11), Lines{"arstn"}},
    {CursorPos(0, 5), CursorPos(1, 6), Lines{"arstnarstn"}},
  };

  for (const Case& test : cases) {
    Lines editRegion = fullBuffer.getSpan(test.begin, test.end);
    TransformBoundary boundary(fullBuffer, test.begin, test.end);

    TransformResult result = optimizer.optimizePureDeletion(
        editRegion, boundary,
        TransformOptimizerParams{}
            .withMaxResults(200)
            .withMaxResultsPerStartPos(5));

    ASSERT_GT(result.resultCount(), 0u);
    int checked = 0;
    for (size_t i = 0; i < result.resultCount(); i++) {
      const auto& bucket = result.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos localPos = fromFlatIndex(static_cast<int>(i), editRegion);
      ASSERT_TRUE(localPos.isValid()) << "Invalid local position at bucket " << i;
      CursorPos fullPos = toFullBufferPos(localPos, test.begin);
      for (const Result& resultForStart : bucket) {
        EXPECT_TRUE(OracleReplay::matches(
            oracle, fullBuffer, fullPos, resultForStart.getSequence().str(),
            test.goal, nullopt, Mode::Normal,
            "straddled pure deletion all results"))
            << "begin=" << test.begin << " end=" << test.end
            << " bucket=" << i << " localPos=" << localPos
            << " fullPos=" << fullPos
            << " seq='" << resultForStart.getSequence() << "'";
        checked++;
      }
    }
    EXPECT_GT(checked, 0);
  }
}
