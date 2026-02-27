// tests/EditOptimizer/RegressionTests.cpp
//
// Regression tests for previously failing EditOptimizer paths.
// Run: ./build/tests/vimficiency_tests --gtest_filter="EditOptimizerRegression.*"

#include <climits>
#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Interpreter/EditInterpreter.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/TestUtils.h"
#include "Types/Lines.h"

using namespace std;

namespace {

struct EmbeddedCase {
  Lines fullBuffer;
  Lines editRegion;
  Lines goalLines;
  EditBoundary boundary;
  CursorPos firstPos;
  CursorPos endPos;  // exclusive
};

EmbeddedCase buildSmallEmbeddedCaseSeed465950() {
  // Reproduces benchmark shape used by EditOpt/SmallEmbeddedChange/3.
  // The historical failure was triggered with this seed.
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
  EditBoundary boundary(fullBuffer, firstPos, endPos);
  Lines goalLines = randomLines(static_cast<int>(editRegion.size()), 4, 8);

  return {fullBuffer, editRegion, goalLines, boundary, firstPos, endPos};
}

}  // namespace

class EditOptimizerRegression : public ::testing::Test {
protected:
  Config config = Config::uniform();
  EditOptimizerParams params = EditOptimizerParams{}.withMaxResults(INT_MAX);
  EditOptimizer opt{config};
};

TEST_F(EditOptimizerRegression, SmallEmbeddedSeed465950_ReplayBoundaryPathMatchesGoal) {
  EmbeddedCase test = buildSmallEmbeddedCaseSeed465950();
  ASSERT_NE(test.editRegion, test.goalLines);

  EditResult result = opt.optimizeEdit(test.editRegion, test.goalLines, test.boundary, params);
  const auto& results = result.getResults();
  int expectedPositions = 0;
  for (const string& line : test.editRegion) {
    expectedPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }
  ASSERT_EQ(results.size(), static_cast<size_t>(expectedPositions));

  // Historical benchmark failure was an internal replay assert; this ensures
  // the optimizer completes and still yields a result for that start index.
  ASSERT_GT(results.size(), 2u);
  EXPECT_FALSE(results[1].empty());
}

TEST_F(EditOptimizerRegression, BoundaryAwareReplayPrefixKeepsXApplicable) {
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

  // Sequence "Dd)jdaW" was valid under old (buggy) d) that didn't consume the
  // newline. With the fix, d) collapses lines so j would fail. Use "DdaW"
  // which still exercises boundary-aware prefix replay.
  for (const ParsedEdit& op : Edit::parseEdits("DdaW")) {
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

TEST_F(EditOptimizerRegression, CountedDwWithLinesBelowBoundaryMatchesLocalSemantics) {
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

#if !defined(NDEBUG)
TEST_F(EditOptimizerRegression, WithoutBoundaryContext_PrefixThenXTriggersDebugAssert) {
  EmbeddedCase test = buildSmallEmbeddedCaseSeed465950();

  Lines replayLines = test.editRegion;
  replayLines[0] = test.boundary.prefix() + replayLines[0];
  replayLines[replayLines.lastLine()] += test.boundary.suffix();

  int leftOffset = test.boundary.leftColOffset();
  ASSERT_GT(static_cast<int>(replayLines[0].size()), leftOffset + 1);

  EXPECT_DEATH(
      {
        CursorPos pos(0, leftOffset + 1);
        Mode mode = Mode::Normal;
        string lastEditCmd;
        for (const ParsedEdit& op : Edit::parseEdits("Dd)jdaWX")) {
          // Intentional: no boundary offsets, reproduces historical mismatch.
          Edit::applyEdit(replayLines, pos, mode, op, &lastEditCmd,
                          /*hasLinesBelow=*/false);
        }
      },
      "X requires more chars before cursor");
}
#endif
