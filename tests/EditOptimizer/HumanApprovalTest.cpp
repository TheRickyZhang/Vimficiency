// tests/EditOptimizer/HumanApprovalTest.cpp
//
// Human-verified examples for EditOptimizer output quality.
// Since no ground truth optimizer exists, we manually verify that outputs
// are sensible and contain expected efficient sequences.
// (first print output, then add various assertions on expected output)

#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Optimizer/OptimizerParams.h"
#include "Utils/Lines.h"
#include "Utils/TestUtils.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class EditOptimizerHumanApprovalTests : public ::testing::Test {
protected:
  inline static const Config config = Config::uniform();
  inline static NavContext navContext = NavContext();
  inline static OptimizerParams params = OptimizerParams(40, 5e4, 1.0, 3.0);
  inline static EditOptimizer opt = EditOptimizer(config, params);

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

// =============================================================================
// EditOptimizer Examples
// =============================================================================

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionSingleWord) {
  // Delete a short word
  Lines initialLines = {"arstn"};
  EditBoundary boundary(initialLines, {0, 0}, initialLines.lastPos());

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);

  // printResultsDebug(res, "Delete single word");
  // Single word should use dw or de, not visual mode (too short)
  ASSERT_TRUE(res[0].isValid());
  string seq = res[0].getSequenceString();
  EXPECT_TRUE(seq.find("dw") != string::npos || seq.find("de") != string::npos)
      << "Expected dw or de for single word, got: " << seq;
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionMultipleLines) {
  // Delete multiple words on single line
  Lines initialLines = {
    "aa bb",
    "arst neio"
  };
  EditBoundary boundary(initialLines, {0, 0}, initialLines.lastPos());

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);

  // printResultsDebug(res, "Delete multiple lines");
  // ASSERT_TRUE(all results costs are <= 4 (always has option of dddd));
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTop) {
  // Delete content with prefix on first line (no suffix)
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  // Edit region: from " " in first line to last char
  Position firstPos(0, 5), lastPos(1, 10);
  Lines editRegion = fullBuffer.getSpan(firstPos, lastPos);
  EditBoundary boundary(fullBuffer, firstPos, lastPos);

  vector<Result> res = opt.optimizePureDeletion(editRegion, boundary);
  // printResultsDebug(res, "Delete straddle top");
}


TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionStraddleBottom) {
  // Delete content with suffix on last line (no prefix)
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  // Edit region: from first char to " " in second line
  Position firstPos(0, 0), lastPos(1, 5);
  Lines editRegion = fullBuffer.getSpan(firstPos, lastPos);
  EditBoundary boundary(fullBuffer, firstPos, lastPos);

  vector<Result> res = opt.optimizePureDeletion(editRegion, boundary);
  // printResultsDebug(res, "Delete straddle bottom");
  // ASSERT_TRUE(finds costs <= 5, like dddw)
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTopAndBottom) {
  // Delete content with both prefix and suffix (middle portion of two lines)
  Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  // Edit region: from first " " to second " " (middle portion)
  Position firstPos(0, 5), lastPos(1, 5);
  Lines editRegion = fullBuffer.getSpan(firstPos, lastPos);
  EditBoundary boundary(fullBuffer, firstPos, lastPos);

  vector<Result> res = opt.optimizePureDeletion(editRegion, boundary);
  // printResultsDebug(res, "Delete straddle top and bottom");
  // ASSERT_TRUE(res[0] == "vjd" (best result by far))
  // ASSERT_TRUE(finds cost <= 7)
}


TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionMultiLineFull) {
  // Delete multi-line content with no boundary
  // Visual mode v}d should be efficient here
  Lines initialLines = {
    "hello world",
    "foo bar baz",
    "one two three",
  };
  EditBoundary boundary(initialLines, {0, 0}, initialLines.lastPos());

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);
  // printResultsDebug(res, "Delete multi-line full");
  // ASSERT_TRUE(all valid)
}

// TODO verify
TEST_F(EditOptimizerHumanApprovalTests, Edit_Replacement_Multiline) {
  vector<Result> results;
  Lines initialLines = {
    "hello",
    "world"
  };
  Lines goalLines = {
    "bello",
    "worth"
  };
  EditResult res = opt.optimizeEdit(initialLines, goalLines, EditBoundary());
  // printResultsDebug(res.replaceResults, "multi line");
  // ASSERT_FALSE(res.replaceResults.empty()) << "Should find replacement strategy";
}
