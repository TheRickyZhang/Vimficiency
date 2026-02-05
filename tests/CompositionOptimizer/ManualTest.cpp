// tests/CompositionOptimizer/ManualTest.cpp
//
// Manual tests for CompositionOptimizer with hardcoded setups.
// Verifies optimizer returns results and sequences achieve goal state.
// For random/stress tests, see OutputCorrectnessTest.cpp (when added).
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="CompositionOptimizer_ManualTest.*"

#include <gtest/gtest.h>
#include <memory>

#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class CompositionOptimizer_ManualTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  // Verify at least one result exists and all results achieve goal state
  void expectHasValidResults(
      const vector<Result>& results,
      const Lines& initial,
      Position initialPos,
      const Lines& goal,
      const string& testContext = "") {

    ASSERT_FALSE(results.empty())
        << "No results returned" << (testContext.empty() ? "" : " (" + testContext + ")");

    for (size_t i = 0; i < results.size(); i++) {
      EXPECT_TRUE(results[i].isValid())
          << "Result " << i << " is invalid"
          << (testContext.empty() ? "" : " (" + testContext + ")");

      const auto& seq = results[i].getSequenceString();
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

      EXPECT_EQ(nvim.lines, goal)
          << "Lines mismatch" << (testContext.empty() ? "" : " (" + testContext + ")")
          << " for result " << i << " seq='" << seq << "' from " << initialPos << "\n"
          << "  Initial: " << initial << "\n"
          << "  Goal:    " << goal << "\n"
          << "  Got:     " << nvim.lines;
    }
  }
};

unique_ptr<NeovimOracle> CompositionOptimizer_ManualTest::oracle;

// =============================================================================
// Single Edit Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_SimpleSubstitution) {
  // Simple word change on same line
  Lines initial = {"hello world"};
  Lines goal = {"hello there"};
  Position initialPos(0, 0);
  Position goalPos(0, 10);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  for(Result r : results) cout << r.sequence << endl;
  expectHasValidResults(results, initial, initialPos, goal, "simple substitution");
}

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_AtCursor) {
  // Cursor already at edit position
  Lines initial = {"aaa"};
  Lines goal = {"bbb"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "at cursor");
}

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_CursorAfterEdit) {
  // Cursor is after the edit region - needs backward motion
  Lines initial = {"aaa bbb"};
  Lines goal = {"xxx bbb"};
  Position initialPos(0, 6);  // Cursor at end
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "cursor after edit");
}

// =============================================================================
// Multiple Edit Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, TwoEdits_SameLine) {
  // Two changes on the same line
  Lines initial = {"aaa bbb ccc"};
  Lines goal = {"xxx bbb yyy"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "two edits same line");
}

TEST_F(CompositionOptimizer_ManualTest, TwoEdits_DifferentLines) {
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"xxx", "bbb", "yyy"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "two edits different lines");
}

// =============================================================================
// Pure Insertion / Deletion Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, PureInsertion) {
  Lines initial = {"hello"};
  Lines goal = {"hello world"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "pure insertion");
}

TEST_F(CompositionOptimizer_ManualTest, PureDeletion) {
  // Delete text, leaving nothing
  Lines initial = {"hello world"};
  Lines goal = {"hello"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "pure deletion");
}

// =============================================================================
// Line-Level Operations
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, DeleteEntireLine) {
  // Delete middle line
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"aaa", "ccc"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "delete entire line");
}

TEST_F(CompositionOptimizer_ManualTest, InsertNewLine) {
  // Insert a new line
  Lines initial = {"aaa", "ccc"};
  Lines goal = {"aaa", "bbb", "ccc"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "insert new line");
}

// =============================================================================
// No Change Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, NoChange_IdenticalBuffers) {
  // When initial == goal, should return empty result or no-op
  Lines initial = {"hello world"};
  Lines goal = {"hello world"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  // Empty results is valid (no changes needed)
  // If there are results, verify they don't break the buffer
  if (!results.empty()) {
    expectHasValidResults(results, initial, initialPos, goal, "no change");
  }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_EmptyToContent) {
  // Empty line to content
  Lines initial = {""};
  Lines goal = {"hello"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "empty to content");
}

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_ContentToEmpty) {
  // Content to empty line
  Lines initial = {"hello"};
  Lines goal = {""};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "content to empty");
}

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_SingleCharChange) {
  // Single character substitution
  Lines initial = {"abc"};
  Lines goal = {"xbc"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "single char change");
}

// =============================================================================
// Text Object Shortcut Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerQuote_CursorBefore) {
  // Cursor before quoted region - ci" should work
  Lines initial = {"foo \"hello\" bar"};
  Lines goal = {"foo \"goodbye\" bar"};
  Position initialPos(0, 0);  // Cursor at 'f'
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci" is among the results and produces correct output
  bool foundValidCiQuote = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    if (seq.keys.find("ci\"") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
      if (nvim.lines == goal) {
        foundValidCiQuote = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiQuote) << "Expected a valid ci\" result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerQuote_CursorInside) {
  // Cursor at opening quote - ci" should work
  Lines initial = {"\"hello\""};
  Lines goal = {"\"goodbye\""};
  Position initialPos(0, 0);  // Cursor at opening quote
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci" is among the results and produces correct output
  bool foundValidCiQuote = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    if (seq.keys.find("ci\"") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
      if (nvim.lines == goal) {
        foundValidCiQuote = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiQuote) << "Expected a valid ci\" result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_AroundQuote) {
  // Test around quote at end of line (no trailing whitespace issue)
  // a" at EOL takes the quotes and leading space instead
  Lines initial = {"foo \"hello\""};
  Lines goal = {"foogoodbye"};  // a" takes " \"hello\"" -> leading space + quotes
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify we get a working result
  bool foundValidResult = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
    if (nvim.lines == goal) {
      foundValidResult = true;
      break;
    }
  }
  EXPECT_TRUE(foundValidResult) << "Expected at least one valid result";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerParen) {
  // Inner parentheses text object
  Lines initial = {"foo (hello) bar"};
  Lines goal = {"foo (goodbye) bar"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci( is among the results and produces correct output
  bool foundValidCiParen = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    if (seq.keys.find("ci(") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
      if (nvim.lines == goal) {
        foundValidCiParen = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiParen) << "Expected a valid ci( result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerBrace) {
  // Inner braces text object
  Lines initial = {"foo {hello} bar"};
  Lines goal = {"foo {goodbye} bar"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci{ is among the results and produces correct output
  bool foundValidCiBrace = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    if (seq.keys.find("ci{") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
      if (nvim.lines == goal) {
        foundValidCiBrace = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiBrace) << "Expected a valid ci{ result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerBracket) {
  // Inner square brackets text object
  Lines initial = {"foo [hello] bar"};
  Lines goal = {"foo [goodbye] bar"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci[ is among the results and produces correct output
  bool foundValidCiBracket = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    if (seq.keys.find("ci[") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
      if (nvim.lines == goal) {
        foundValidCiBracket = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiBracket) << "Expected a valid ci[ result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_NotValidAfterQuote) {
  // Cursor after a quote of the same type - ci" would pair with the earlier quote
  // This tests that the optimizer still finds a valid solution using other methods
  Lines initial = {"a\"b \"hello\" bar"};
  Lines goal = {"a\"b \"goodbye\" bar"};
  Position initialPos(0, 3);  // Cursor at ' ', after first quote pair
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify at least one result produces the goal
  bool foundValid = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
    if (nvim.lines == goal) {
      foundValid = true;
      break;
    }
  }
  EXPECT_TRUE(foundValid) << "Expected at least one valid result";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_NestedBrackets) {
  // Nested brackets - from position 0, ci( uses the outermost pair
  // Start from position 5 (on inner open paren) to target inner pair
  Lines initial = {"foo ((hello)) bar"};
  Lines goal = {"foo ((goodbye)) bar"};
  Position initialPos(0, 5);  // Cursor on inner '('
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify at least one result produces the goal
  bool foundValid = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
    if (nvim.lines == goal) {
      foundValid = true;
      break;
    }
  }
  EXPECT_TRUE(foundValid) << "Expected at least one valid result";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_SingleQuote) {
  // Single quote text object
  Lines initial = {"foo 'hello' bar"};
  Lines goal = {"foo 'goodbye' bar"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci' is among the results and produces correct output
  bool foundValidCiSingleQuote = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequenceString();
    if (seq.keys.find("ci'") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);
      if (nvim.lines == goal) {
        foundValidCiSingleQuote = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiSingleQuote) << "Expected a valid ci' result that produces the goal";
}

// =============================================================================
// Pure Insertion Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_NewLineBetween) {
  // Insert new line between existing lines: should use 'o' shortcut
  Lines initial = {"a", "c"};
  Lines goal = {"a", "b", "c"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "new line insertion");

  // Check that 'o' is used (optimal for this case)
  bool usesO = false;
  for (const Result& r : results) {
    if (r.sequence.keys.find("ob") != string::npos) {
      usesO = true;
      break;
    }
  }
  EXPECT_TRUE(usesO) << "Expected 'o' shortcut for new line insertion";
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_AppendToLine) {
  // Append to end of line: should use 'A' shortcut
  Lines initial = {"a", "c"};
  Lines goal = {"ab", "c"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "append to line");

  // Check that 'A' is used (optimal for this case)
  bool usesA = false;
  for (const Result& r : results) {
    if (r.sequence.keys.find("Ab") != string::npos) {
      usesA = true;
      break;
    }
  }
  EXPECT_TRUE(usesA) << "Expected 'A' shortcut for append insertion";
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_AppendWithNewline) {
  // Append 'b' and create empty line: should be single diff after merge
  // Optimal: A + b + <CR> + <Esc>
  Lines initial = {"a", "c"};
  Lines goal = {"ab", "", "c"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "append with newline");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_InsertAtStart) {
  // Insert at start of line: should use 'I' shortcut (if at first non-blank)
  Lines initial = {"a", "c"};
  Lines goal = {"ba", "c"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "insert at start");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_InsertInMiddle) {
  // Insert in middle of line: should use 'i' after navigation
  Lines initial = {"abc", "d"};
  Lines goal = {"axbc", "d"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  expectHasValidResults(results, initial, initialPos, goal, "insert in middle");
}

// =============================================================================
// Note: More comprehensive stress tests should be added in OutputCorrectnessTest.cpp
// =============================================================================
