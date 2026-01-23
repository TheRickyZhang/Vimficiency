// tests/EditOptimizer/HumanApprovalTest.cpp
//
// Human-verified examples for EditOptimizer output quality.
// Since no ground truth optimizer exists, we manually verify that outputs
// are sensible and contain expected efficient sequences.
//
// Workflow:
// 1. Run test with DISABLED_ prefix first to see output
// 2. Verify output is sensible
// 3. Add assertions for expected sequences
// 4. Remove DISABLED_ prefix

#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Utils/Lines.h"
#include "Utils/TestUtils.h"  // hasSequence, hasSequenceStartingWith, printResultsDebug

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class EditOptimizerHumanApprovalTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext EditOptimizerHumanApprovalTests::navContext;

// =============================================================================
// EditOptimizer Examples
// =============================================================================

TEST_F(EditOptimizerHumanApprovalTests, DISABLED_Edit_SimpleDeletion) {
  // Delete a short word
  Lines lines = {"hello"};
  EditBoundary boundary(lines, {0, 0}, lines.lastPos());
  EditOptimizer opt(config, OptimizerParams(10, 1e4, 1.0, 2.0));

  EditResult res = opt.optimizeEdit(lines, {""}, boundary);

  printResultsDebug(res.typeAllResults, "Delete 'hello'");

  EXPECT_TRUE(res.typeAllResults[0].isValid());

  // TODO: Add assertions after reviewing output
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
