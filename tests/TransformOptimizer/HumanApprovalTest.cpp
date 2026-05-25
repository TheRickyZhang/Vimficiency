// tests/TransformOptimizer/HumanApprovalTest.cpp
//
// Human-verified examples for TransformOptimizer output quality.
// Since no ground truth optimizer exists, we manually verify that outputs
// are sensible and contain expected efficient sequences.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="TransformOptimizerHumanApprovalTests.*"

#include <gtest/gtest.h>

#include <memory>

#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Boundary/TransformBoundary.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class TransformOptimizerHumanApprovalTests : public ::testing::Test {
protected:
  inline static const Config config = Config::uniform();
  inline static NavContext navContext = NavContext();
  inline static TransformOptimizerParams params;
  inline static TransformOptimizer opt{config};
  inline static unique_ptr<NeovimOracle> oracle;

  static void SetUpTestSuite() {
    navContext = NavContext();
    oracle = make_unique<NeovimOracle>();
  }

  static void TearDownTestSuite() { oracle.reset(); }

  static TransformResult pureDeletionResult(
      const Lines& initialLines,
      TransformBoundary boundary,
      TransformOptimizerParams p) {
    return opt.optimizePureDeletion(initialLines, boundary, p);
  }

  static Lines deleteSpan(Lines lines, CursorPos begin, CursorPos end) {
    CursorPos pos = begin;
    VimCore::deleteRangeAndUpdatePos(lines, CharRange(begin, end), pos, Mode::Normal);
    return lines;
  }

  static void verifyInitialResultReachesGoal(
      const TransformResult& result,
      const Lines& source,
      CursorPos startPos,
      const Lines& goal,
      const string& context) {
    ASSERT_GT(result.resultCount(), 0u) << context;
    ASSERT_FALSE(result.getResults()[0].empty()) << context;

    const Result& r = result.getResults()[0][0];
    EXPECT_TRUE(OracleReplay::matches(
        *oracle, source, startPos, r.getSequence().str(), goal,
        nullopt, Mode::Normal, context));
  }

  static TransformResult verifyPureDeletion(
      const Lines& fullBuffer,
      CursorPos begin,
      CursorPos end,
      const string& context) {
    Lines editRegion = fullBuffer.getSpan(begin, end);
    TransformBoundary boundary(fullBuffer, begin, end);
    TransformResult result = pureDeletionResult(editRegion, boundary, params);
    verifyInitialResultReachesGoal(
        result, fullBuffer, begin, deleteSpan(fullBuffer, begin, end), context);
    return result;
  }
};

// =============================================================================
// TransformOptimizer Examples
// =============================================================================

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionSingleWord) {
  Lines initialLines = {"arstn"};
  TransformResult editRes = verifyPureDeletion(
      initialLines, CursorPos(0, 0), initialLines.endPos(), "single word deletion");
  const auto& res = editRes.getResults();

  ASSERT_FALSE(res[0].empty());
  const auto& seq = res[0][0].getSequence();
  EXPECT_TRUE(seq.view().find("dw") != string::npos || seq.view().find("de") != string::npos)
      << "Expected dw or de for single word, got: " << seq;
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionMultipleLines) {
  Lines initialLines = {
    "aa bb",
    "arst neio"
  };
  verifyPureDeletion(
      initialLines, CursorPos(0, 0), initialLines.endPos(), "multiple-line deletion");
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTop) {
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  CursorPos firstPos(0, 5), endPos(1, 11);
  verifyPureDeletion(fullBuffer, firstPos, endPos, "straddle top deletion");
}


TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionStraddleBottom) {
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  CursorPos firstPos(0, 0), endPos(1, 6);
  verifyPureDeletion(fullBuffer, firstPos, endPos, "straddle bottom deletion");
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTopAndBottom) {
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  CursorPos firstPos(0, 5), endPos(1, 6);
  TransformResult editRes = verifyPureDeletion(
      fullBuffer, firstPos, endPos, "straddle top and bottom deletion");
  ASSERT_FALSE(editRes.getResults()[0].empty());
  EXPECT_EQ(editRes.getResults()[0][0].getSequence(), "vjd");
}


TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionMultiLineFull) {
  Lines initialLines = {
    "hello world",
    "foo bar baz",
    "one two three",
  };
  verifyPureDeletion(
      initialLines, CursorPos(0, 0), initialLines.endPos(), "full multi-line deletion");
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_Replacement_Multiline) {
  Lines initialLines = {
    "hello",
    "world"
  };
  Lines goalLines = {
    "bello",
    "worth"
  };
  TransformResult res = opt.optimizeTransform(initialLines, goalLines, TransformBoundary(), params);
  verifyInitialResultReachesGoal(
      res, initialLines, CursorPos(0, 0), goalLines, "multi-line replacement");
}
