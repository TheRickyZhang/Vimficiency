#include "CompositionOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_SimpleSubstitution) {
  // Simple word change on same line
  Lines initial = {"hello world"};
  Lines goal = {"hello there"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 10);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "simple substitution");
}

TEST_F(CompositionOptimizer_ManualTest, SingleEdit_AtCursor) {
  // Cursor already at edit position
  Lines initial = {"aaa"};
  Lines goal = {"bbb"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "at cursor");
}


// =============================================================================
// Multiple Edit Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, TwoEdits_SameLine) {
  // Two changes on the same line
  Lines initial = {"aaa bbb ccc"};
  Lines goal = {"xxx bbb yyy"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "two edits same line");
}

TEST_F(CompositionOptimizer_ManualTest, TwoEdits_DifferentLines) {
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"xxx", "bbb", "yyy"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "two edits different lines");
}

// =============================================================================
// Pure Insertion / Deletion Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, PureInsertion) {
  Lines initial = {"hello"};
  Lines goal = {"hello world"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "pure insertion");
}

TEST_F(CompositionOptimizer_ManualTest, PureDeletion) {
  // Delete text, leaving nothing
  Lines initial = {"hello world"};
  Lines goal = {"hello"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "pure deletion");
}

// =============================================================================
// Line-Level Operations
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, DeleteEntireLine) {
  // Delete middle line
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"aaa", "ccc"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "delete entire line");
}

TEST_F(CompositionOptimizer_ManualTest, InsertNewLine) {
  // Insert a new line
  Lines initial = {"aaa", "ccc"};
  Lines goal = {"aaa", "bbb", "ccc"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "insert new line");
}

// =============================================================================
// No Change Tests
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, NoChange_IdenticalBuffers) {
  // When initial == goal AND initialPos == goalPos, the optimizer emits a
  // single trivially-satisfied vacuous result (empty sequence).
  Lines initial = {"hello world"};
  Lines goal = {"hello world"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_EQ(results.size(), 1u);
  EXPECT_TRUE(results[0].getSequence().empty());
  EXPECT_EQ(compResult.totalEdits(), 0);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_EmptyToContent) {
  // Empty line to content
  Lines initial = {""};
  Lines goal = {"hello"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "empty to content");
}

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_ContentToEmpty) {
  // Content to empty line
  Lines initial = {"hello"};
  Lines goal = {""};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "content to empty");
}

TEST_F(CompositionOptimizer_ManualTest, EdgeCase_SingleCharChange) {
  // Single character substitution
  Lines initial = {"abc"};
  Lines goal = {"xbc"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "single char change");
}

// =============================================================================
// Text Object Shortcut Tests
// =============================================================================

}  // namespace
