#include "CompositionOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_NewLineBetween) {
  // Insert new line between existing lines: should use 'o' shortcut
  Lines initial = {"a", "c"};
  Lines goal = {"a", "b", "c"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "new line insertion");

  // Check that 'o' is used (optimal for this case)
  bool usesO = false;
  for (const Result& r : results) {
    if (r.getSequence().view().find("ob") != string::npos) {
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
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "append to line");

  // Check that 'A' is used (optimal for this case)
  bool usesA = false;
  for (const Result& r : results) {
    if (r.getSequence().view().find("Ab") != string::npos) {
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
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "append with newline");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_NewLineWithWhitespaceOnlyContent) {
  Lines initial = {",,a.fccb", "ddc cfe .", " dd"};
  Lines goal = {",,a.fccb", "ddc cfe .", " dd", " "};
  CursorPos initialPos(2, 1);
  CursorPos goalPos(2, 2);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  ASSERT_FALSE(results.empty());
  for (size_t i = 0; i < results.size(); i++) {
    EXPECT_TRUE(OracleReplay::matches(
        *oracle, initial, initialPos, results[i].getSequence().str(),
        goal, goalPos, Mode::Normal,
        "newline whitespace-only result " + to_string(i)));
  }
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_MidLineNewlinePreservesWhitespaceSuffix) {
  Lines initial = {", ba", "afd  ", "cbcc  b."};
  Lines goal = {", ba", "afdfadd", "afd  ", "cbcc  b."};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(
      results, initial, initialPos, goal, "mid-line newline insertion");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_InsertAtStart) {
  // Insert at start of line: should use 'I' shortcut (if at first non-blank)
  Lines initial = {"a", "c"};
  Lines goal = {"ba", "c"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "insert at start");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_InsertInMiddle) {
  // Insert in middle of line: should use 'i' after navigation
  Lines initial = {"abc", "d"};
  Lines goal = {"axbc", "d"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "insert in middle");
}

}  // namespace
