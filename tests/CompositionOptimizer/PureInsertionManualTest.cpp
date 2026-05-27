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

}  // namespace
