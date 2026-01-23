// tests/Optimizer/HumanApprovalTests.cpp
//
// Human-verified examples for optimizer output quality.
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
#include "Optimizer/MovementOptimizer.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "Boundary/EditBoundary.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class HumanApprovalTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  // Check if any result contains the given sequence
  static bool hasSequence(const vector<Result>& results, const string& seq) {
    return any_of(results.begin(), results.end(),
        [&seq](const Result& r) { return r.getSequenceString() == seq; });
  }

  // Check if any result contains a sequence starting with prefix
  static bool hasSequenceStartingWith(const vector<Result>& results, const string& prefix) {
    return any_of(results.begin(), results.end(),
        [&prefix](const Result& r) {
          return r.getSequenceString().substr(0, prefix.size()) == prefix;
        });
  }

  // Print results for manual inspection
  static void printResults(const vector<Result>& results, const string& description) {
    cerr << "\n=== " << description << " ===" << endl;
    for (size_t i = 0; i < results.size() && i < 10; i++) {
      cerr << "  " << i << ": " << results[i].getSequenceString()
           << " (cost=" << results[i].keyCost << ")" << endl;
    }
  }
};

NavContext HumanApprovalTests::navContext;

// =============================================================================
// MovementOptimizer Examples
// =============================================================================

TEST_F(HumanApprovalTests, DISABLED_Movement_SimpleHorizontal) {
  // Moving right 5 characters: "lllll" vs "5l" vs word motions
  Lines lines = {"hello world test"};
  Position start(0, 0);
  Position end(0, 5);  // At space before "world"

  MovementOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "lllll", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
  );

  printResults(results, "Simple horizontal movement 0→5");

  EXPECT_FALSE(results.empty()) << "Should find at least one path";

  // TODO: Add assertions after reviewing output
  // EXPECT_TRUE(hasSequence(results, "5l") || hasSequence(results, "lllll"));
}

TEST_F(HumanApprovalTests, Movement_VerticalJump) {
  // Moving down 3 lines
  Lines lines = {"line one", "line two", "line three", "line four"};
  Position start(0, 0);
  Position end(3, 0);

  MovementOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "jjj", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
  );

  // printResults(results, "Vertical jump 3 lines");

  EXPECT_FALSE(results.empty());

  // G goes to last line, should be found
  EXPECT_TRUE(hasSequence(results, "G"))
      << "G should be found for jumping to last line";

  // 3j should also be viable
  EXPECT_TRUE(hasSequence(results, "3j") || hasSequence(results, "jjj"))
      << "Should find count-based or repeated j";
}

TEST_F(HumanApprovalTests, Movement_WordMotions) {
  // Navigate using word motions
  Lines lines = {"one two three four five"};
  Position start(0, 0);
  Position end(0, 14);  // Start of "four"

  MovementOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "www", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
  );

  // printResults(results, "Word motions to 'four'");

  EXPECT_FALSE(results.empty());

  // Should find word-based navigation
  EXPECT_TRUE(hasSequence(results, "3w") || hasSequence(results, "www"))
      << "Should find word-based navigation";
}

TEST_F(HumanApprovalTests, Movement_MixedMotions) {
  // Complex movement requiring mixed motions
  Lines lines = {"first line here", "second line", "third line end"};
  Position start(0, 0);
  Position end(2, 11);  // At "end"

  MovementOptimizer opt(config);
  auto results = opt.optimize(
    lines, start, RunningEffort(), end, "jjllllllllll", navContext,
    MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(15, 1e4, 1.0, 2.0)
  );

  // printResults(results, "Mixed motions to line 2, col 11");

  EXPECT_FALSE(results.empty());

  // Should find something more efficient than 12 l's
  bool foundEfficient = false;
  for (const auto& r : results) {
    if (r.getSequenceString().size() < 12) {
      foundEfficient = true;
      break;
    }
  }
  EXPECT_TRUE(foundEfficient) << "Should find sequence shorter than naive approach";
}

// =============================================================================
// EditOptimizer Examples
// =============================================================================

TEST_F(HumanApprovalTests, DISABLED_Edit_SimpleDeletion) {
  // Delete a short word
  Lines lines = {"hello"};
  EditBoundary boundary(lines, {0, 0}, lines.lastPos());
  EditOptimizer opt(config, OptimizerParams(10, 1e4, 1.0, 2.0));

  EditResult res = opt.optimizeEdit(lines, {""}, boundary);

  printResults(res.typeAllResults, "Delete 'hello'");

  EXPECT_TRUE(res.typeAllResults[0].isValid());

  // TODO: Add assertions after reviewing output
}

TEST_F(HumanApprovalTests, Edit_Replacement_SingleChar) {
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

TEST_F(HumanApprovalTests, Edit_Replacement_MiddleChar) {
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

TEST_F(HumanApprovalTests, Edit_Replacement_MultipleChars) {
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
