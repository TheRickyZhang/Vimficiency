// tests/TransformOptimizer/HumanApprovalTest.cpp
//
// Human-verified examples for TransformOptimizer output quality.
// Since no ground truth optimizer exists, we manually verify that outputs
// are sensible and contain expected efficient sequences.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TransformOptimizerHumanApprovalTests.*"

#include <gtest/gtest.h>

#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Boundary/TransformBoundary.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Types/Lines.h"

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

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  static TransformResult pureDeletionResult(
      const Lines& initialLines,
      TransformBoundary boundary,
      TransformOptimizerParams p) {
    return opt.optimizePureDeletion(initialLines, boundary, p);
  }
};

// =============================================================================
// TransformOptimizer Examples
// =============================================================================

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionSingleWord) {
  // Delete a short word
  Lines initialLines = {"arstn"};
  TransformBoundary boundary(initialLines, {0, 0}, initialLines.endPos());

  TransformResult editRes = pureDeletionResult(initialLines, boundary, params);
  const auto& res = editRes.getResults();

  // printResultsDebug(res, "Delete single word");
  // Single word should use dw or de, not visual mode (too short)
  ASSERT_FALSE(res[0].empty());
  const auto& seq = res[0][0].getSequence();
  EXPECT_TRUE(seq.view().find("dw") != string::npos || seq.view().find("de") != string::npos)
      << "Expected dw or de for single word, got: " << seq;
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionMultipleLines) {
  // Delete multiple words on single line
  Lines initialLines = {
    "aa bb",
    "arst neio"
  };
  TransformBoundary boundary(initialLines, {0, 0}, initialLines.endPos());

  TransformResult editRes = pureDeletionResult(initialLines, boundary, params);
  const auto& res = editRes.getResults();

  // printResultsDebug(res, "Delete multiple lines");
  // ASSERT_TRUE(all results costs are <= 4 (always has option of dddd));
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTop) {
  // Delete content with prefix on first line (no suffix)
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  // Edit region: from " " in first line to last char
  CursorPos firstPos(0, 5), endPos(1, 11);
  Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
  TransformBoundary boundary(fullBuffer, firstPos, endPos);

  TransformResult editRes = pureDeletionResult(editRegion, boundary, params);
  const auto& res = editRes.getResults();
  // printResultsDebug(res, "Delete straddle top");
}


TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionStraddleBottom) {
  // Delete content with suffix on last line (no prefix)
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  // Edit region: from first char to " " in second line
  CursorPos firstPos(0, 0), endPos(1, 6);
  Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
  TransformBoundary boundary(fullBuffer, firstPos, endPos);

  TransformResult editRes = pureDeletionResult(editRegion, boundary, params);
  const auto& res = editRes.getResults();
  // printResultsDebug(res, "Delete straddle bottom");
  // ASSERT_TRUE(finds costs <= 5, like dddw)
}

TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTopAndBottom) {
  // Delete content with both prefix and suffix (middle portion of two lines)
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  // Edit region: from first " " to second " " (middle portion)
  CursorPos firstPos(0, 5), endPos(1, 6);
  Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
  TransformBoundary boundary(fullBuffer, firstPos, endPos);

  TransformResult editRes = pureDeletionResult(editRegion, boundary, params);
  const auto& res = editRes.getResults();
  // printResultsDebug(res, "Delete straddle top and bottom");
  // ASSERT_TRUE(res[0] == "vjd" (best result by far))
  // ASSERT_TRUE(finds cost <= 7)
}


TEST_F(TransformOptimizerHumanApprovalTests, Edit_PureDeletionMultiLineFull) {
  // Delete multi-line content with no boundary
  // Visual mode v}d should be efficient here
  Lines initialLines = {
    "hello world",
    "foo bar baz",
    "one two three",
  };
  TransformBoundary boundary(initialLines, {0, 0}, initialLines.endPos());

  TransformResult editRes = pureDeletionResult(initialLines, boundary, params);
  const auto& res = editRes.getResults();
  // printResultsDebug(res, "Delete multi-line full");
  // ASSERT_TRUE(all valid)
}

// TODO verify
TEST_F(TransformOptimizerHumanApprovalTests, Edit_Replacement_Multiline) {
  vector<Result> results;
  Lines initialLines = {
    "hello",
    "world"
  };
  Lines goalLines = {
    "bello",
    "worth"
  };
  TransformResult res = opt.optimizeTransform(initialLines, goalLines, TransformBoundary(), params);
  // printResultsDebug(res.replaceResults, "multi line");
  // ASSERT_FALSE(res.replaceResults.empty()) << "Should find replacement strategy";
}
