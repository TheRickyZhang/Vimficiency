#include "Unit/CompositionOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerQuote_CursorBefore) {
  // Cursor before quoted region - ci" should work
  Lines initial = {"foo \"hello\" bar"};
  Lines goal = {"foo \"goodbye\" bar"};
  CursorPos initialPos(0, 0);  // Cursor at 'f'
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci" is among the results and produces correct output
  bool foundValidCiQuote = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequence();
    if (seq.view().find("ci\"") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.str());
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
  CursorPos initialPos(0, 0);  // Cursor at opening quote
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci" is among the results and produces correct output
  bool foundValidCiQuote = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequence();
    if (seq.view().find("ci\"") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.str());
      if (nvim.lines == goal) {
        foundValidCiQuote = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiQuote) << "Expected a valid ci\" result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_InnerParen) {
  // Inner parentheses text object
  Lines initial = {"foo (hello) bar"};
  Lines goal = {"foo (goodbye) bar"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci( is among the results and produces correct output
  bool foundValidCiParen = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequence();
    if (seq.view().find("ci(") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.str());
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
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci{ is among the results and produces correct output
  bool foundValidCiBrace = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequence();
    if (seq.view().find("ci{") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.str());
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
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci[ is among the results and produces correct output
  bool foundValidCiBracket = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequence();
    if (seq.view().find("ci[") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.str());
      if (nvim.lines == goal) {
        foundValidCiBracket = true;
        break;
      }
    }
  }
  EXPECT_TRUE(foundValidCiBracket) << "Expected a valid ci[ result that produces the goal";
}

TEST_F(CompositionOptimizer_ManualTest, TextObject_SingleQuote) {
  // Single quote text object
  Lines initial = {"foo 'hello' bar"};
  Lines goal = {"foo 'goodbye' bar"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty()) << "No results returned";

  // Verify ci' is among the results and produces correct output
  bool foundValidCiSingleQuote = false;
  for (const Result& r : results) {
    const auto& seq = r.getSequence();
    if (seq.view().find("ci'") != string::npos) {
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.str());
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

}  // namespace
