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
  inline static OptimizerParams params = OptimizerParams(10, 1e4, 1.0, 2.0);
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

  printResultsDebug(res, "Delete single word");
  // Single word should use dw or de, not visual mode (too short)
  ASSERT_TRUE(res[0].isValid());
  string seq = res[0].getSequenceString();
  EXPECT_TRUE(seq.find("dw") != string::npos || seq.find("de") != string::npos)
      << "Expected dw or de for single word, got: " << seq;
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionMultipleWords) {
  // Delete multiple words on single line
  Lines initialLines = {"hello world foo bar"};
  EditBoundary boundary(initialLines, {0, 0}, initialLines.lastPos());

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);

  printResultsDebug(res, "Delete multiple words");
  // For longer single-line content, visual mode should be competitive
  ASSERT_TRUE(res[0].isValid());
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTop) {
  // Delete a short word
  Lines initialLines = {
    "arstn arstn",
    "arstn arstn",
  };
  // " " in first line, last char
  Position firstPos(0, 5), lastPos(1, 10);
  EditBoundary boundary(initialLines, firstPos, lastPos);

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);
  printResultsDebug(res, "Delete straddle top");
}


TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionStraddleBottom) {
  // Delete a short word
  Lines initialLines = {
    "arstn arstn",
    "arstn arstn",
  };
  // first char, " " in second line
  Position firstPos(0, 0), lastPos(1, 5);
  EditBoundary boundary(initialLines, firstPos, lastPos);

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);
  printResultsDebug(res, "Delete straddle bottom");
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionStraddleTopAndBottom) {
  // Delete content that straddles prefix and suffix on both lines
  Lines initialLines = {
    "arstn arstn",
    "arstn arstn",
  };
  // first " ", second " "
  Position firstPos(0, 5), lastPos(1, 5);
  EditBoundary boundary(initialLines, firstPos, lastPos);

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);
  printResultsDebug(res, "Delete straddle top and bottom");

  // Visual mode should be the best solution here
  ASSERT_TRUE(res[0].isValid());
  string seq = res[0].getSequenceString();
  EXPECT_TRUE(seq.find("v") != string::npos && seq.find("d") != string::npos)
      << "Expected visual mode (v...d) for multi-line straddling, got: " << seq;
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
  printResultsDebug(res, "Delete multi-line full");

  // Visual mode with paragraph motion should be optimal
  ASSERT_TRUE(res[0].isValid());
  string seq = res[0].getSequenceString();
  EXPECT_TRUE(seq.find("v") != string::npos)
      << "Expected visual mode for multi-line full deletion, got: " << seq;
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_PureDeletionTwoLinesNoPrefix) {
  // Delete two lines without prefix - J command should be considered
  Lines initialLines = {
    "first line",
    "second line",
  };
  EditBoundary boundary(initialLines, {0, 0}, initialLines.lastPos());

  vector<Result> res = opt.optimizePureDeletion(initialLines, boundary);
  printResultsDebug(res, "Delete two lines (J candidate)");

  // Should find valid solution
  ASSERT_TRUE(res[0].isValid());
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_Replacement_SingleChar) {
  // Replace single character: hello → jello
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("hello", "jello", config, lastPos, results);

  ASSERT_FALSE(results.empty()) << "Should find replacement strategy";

  string seq = results[0].getSequenceString();
  // Should use r{char} for single character replacement
  EXPECT_TRUE(seq.find("rj") != string::npos)
      << "Should use 'rj' for single char replacement, got: " << seq;
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_Replacement_MiddleChar) {
  // Replace middle character: fresh → frosh
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("fresh", "frosh", config, lastPos, results);

  ASSERT_FALSE(results.empty()) << "Should find replacement strategy";

  string seq = results[0].getSequenceString();
  // Should navigate then replace
  EXPECT_TRUE(seq.find("ro") != string::npos)
      << "Should use 'ro' for replacing 'e' with 'o', got: " << seq;
}

TEST_F(EditOptimizerHumanApprovalTests, Edit_Replacement_MultipleChars) {
  // Replace multiple consecutive chars: abc → xyz
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("abc", "xyz", config, lastPos, results);

  ASSERT_FALSE(results.empty()) << "Should find replacement strategy";

  // Should use R mode for consecutive replacements
  string seq = results[0].getSequenceString();
  EXPECT_TRUE(seq.find("R") != string::npos || seq.find("r") != string::npos)
      << "Should use replace mode, got: " << seq;
}
