// tests/CompositionOptimizer/OutputCorrectnessTest.cpp
//
// Generated property tests for CompositionOptimizer result validity.
// Each case replays a bounded set of top results against Neovim.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="CompositionOptimizerOutputCorrectness.*"

#include <gtest/gtest.h>
#include <memory>

#include "Types/CursorPos.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Types/Lines.h"
#include "Utils/GeneratedProperty.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OptimizerResultChecks.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

class CompositionOptimizerOutputCorrectness : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static const int NUM_ITERATIONS = 30;
  static constexpr size_t MAX_RESULTS_TO_REPLAY = 3;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  void expectTopResultsReachGoal(
      const vector<Result>& results,
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      const string& testContext = "") {
    OptimizerResultChecks::expectTopResultsReplay(
        *oracle, results, initial, initialPos, goal,
        MAX_RESULTS_TO_REPLAY, testContext);
  }

  void expectOptimizationTopResultsReachGoal(
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      CursorPos goalPos,
      const string& testContext = "") {
    auto compResult = opt.optimize(initial, initialPos, goal, goalPos, params);
    expectTopResultsReachGoal(
        compResult.getResults(), initial, initialPos, goal, testContext);
  }
};

unique_ptr<NeovimOracle> CompositionOptimizerOutputCorrectness::oracle;

// =============================================================================
// Single edit generated properties
// =============================================================================

// Single-line substitutions
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_SingleLineEditTopResultsReplay) {
  GeneratedProperty::check({"Composition single-line edit replay", 42, NUM_ITERATIONS}, [&](int) {
    int lineLen = RandomGen::range(8, 20);
    string line = randomWord(lineLen);
    Lines initial = {line};

    int editStart = RandomGen::range(0, max(0, lineLen - 3));
    int editLen = RandomGen::range(1, min(5, lineLen - editStart));
    int editEnd = editStart + editLen;

    int replaceLen = RandomGen::range(1, 6);
    string replacement = randomWord(replaceLen);

    // Build goal
    string goalStr = line.substr(0, editStart) + replacement + line.substr(editEnd);
    Lines goal = {goalStr};

    int cursorCol = RandomGen::range(0, max(0, lineLen - 1));
    CursorPos initialPos(0, cursorCol);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "single-line edit");
  });
}

// Multi-line single edits
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_MultiLineSingleEditTopResultsReplay) {
  GeneratedProperty::check({"Composition multi-line single edit replay", 43, NUM_ITERATIONS}, [&](int) {
    int numLines = RandomGen::range(2, 4);
    Lines initial = randomLines(numLines, 5, 12);

    int editLine = RandomGen::range(0, numLines - 1);
    string& targetLine = initial[editLine];
    int lineLen = static_cast<int>(targetLine.size());
    if (lineLen < 2) return;

    int editStart = RandomGen::range(0, max(0, lineLen - 2));
    int editLen = RandomGen::range(1, min(4, lineLen - editStart));
    int editEnd = editStart + editLen;

    string replacement = randomWord(RandomGen::range(1, 5));

    Lines goal = initial;
    goal[editLine] = targetLine.substr(0, editStart) + replacement + targetLine.substr(editEnd);

    int cursorLine = RandomGen::range(0, numLines - 1);
    int cursorCol = RandomGen::range(0, max(0, static_cast<int>(initial[cursorLine].size()) - 1));
    CursorPos initialPos(cursorLine, cursorCol);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "multi-line single edit");
  });
}

// =============================================================================
// Pure insertion/deletion generated properties
// =============================================================================

// Pure insertions
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_PureInsertionTopResultsReplay) {
  GeneratedProperty::check({"Composition pure insertion replay", 44, NUM_ITERATIONS}, [&](int) {
    int numLines = RandomGen::range(1, 3);
    Lines initial = randomLines(numLines, 4, 10);

    int insertLine = RandomGen::range(0, numLines - 1);
    int lineLen = static_cast<int>(initial[insertLine].size());
    int insertCol = RandomGen::range(0, lineLen);

    string insertText = randomWord(RandomGen::range(2, 6));

    Lines goal = initial;
    goal[insertLine] = initial[insertLine].substr(0, insertCol) +
                       insertText +
                       initial[insertLine].substr(insertCol);

    // Cursor at beginning
    CursorPos initialPos(0, 0);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "pure insertion");
  });
}

// Pure deletions
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_PureDeletionTopResultsReplay) {
  GeneratedProperty::check({"Composition pure deletion replay", 45, NUM_ITERATIONS}, [&](int) {
    int numLines = RandomGen::range(1, 3);
    Lines initial = randomLines(numLines, 8, 15);

    int deleteLine = RandomGen::range(0, numLines - 1);
    int lineLen = static_cast<int>(initial[deleteLine].size());
    if (lineLen < 4) return;

    int deleteStart = RandomGen::range(0, lineLen - 3);
    int deleteLen = RandomGen::range(2, min(6, lineLen - deleteStart));
    int deleteEnd = deleteStart + deleteLen;

    Lines goal = initial;
    goal[deleteLine] = initial[deleteLine].substr(0, deleteStart) +
                       initial[deleteLine].substr(deleteEnd);

    CursorPos initialPos(0, 0);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "pure deletion");
  });
}

// =============================================================================
// Line-Level Operation Tests
// =============================================================================

// Insert new lines
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_InsertNewLineTopResultsReplay) {
  GeneratedProperty::check({"Composition insert new line replay", 46, NUM_ITERATIONS}, [&](int) {
    int numLines = RandomGen::range(2, 3);
    Lines initial = randomLines(numLines, 4, 8);

    int insertAfter = RandomGen::range(0, numLines - 1);
    string newLine = randomWord(RandomGen::range(3, 7));

    Lines goal = initial;
    goal.insert(goal.begin() + insertAfter + 1, newLine);

    CursorPos initialPos(0, 0);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "insert new line");
  });
}

// Delete entire lines
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_DeleteEntireLineTopResultsReplay) {
  GeneratedProperty::check({"Composition delete entire line replay", 47, NUM_ITERATIONS}, [&](int) {
    int numLines = RandomGen::range(3, 4);
    Lines initial = randomLines(numLines, 4, 8);

    int deleteLine = RandomGen::range(1, numLines - 2);

    Lines goal = initial;
    goal.erase(goal.begin() + deleteLine);

    CursorPos initialPos(0, 0);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "delete entire line");
  });
}

// =============================================================================
// Multiple Edit Tests
// =============================================================================

// Two edits on the same line
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_TwoEditsSameLineTopResultsReplay) {
  GeneratedProperty::check({"Composition two edits same line replay", 48, NUM_ITERATIONS}, [&](int) {
    string line = randomWord(3) + " " + randomWord(4) + " " + randomWord(3);
    Lines initial = {line};

    string newFirst = randomWord(3);
    string newLast = randomWord(3);

    size_t firstSpace = line.find(' ');
    size_t lastSpace = line.rfind(' ');
    string middle = line.substr(firstSpace, lastSpace - firstSpace + 1);
    string goalStr = newFirst + middle + newLast;
    Lines goal = {goalStr};

    CursorPos initialPos(0, 0);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "two edits same line");
  });
}

// Two edits on different lines
TEST_F(CompositionOptimizerOutputCorrectness, GeneratedProperty_TwoEditsDifferentLinesTopResultsReplay) {
  GeneratedProperty::check({"Composition two edits different lines replay", 49, NUM_ITERATIONS}, [&](int) {
    Lines initial = randomLines(3, 5, 10);

    Lines goal = initial;
    goal[0] = randomWord(RandomGen::range(4, 8));
    goal[2] = randomWord(RandomGen::range(4, 8));

    CursorPos initialPos(0, 0);
    CursorPos goalPos(0, 0);

    expectOptimizationTopResultsReachGoal(
        initial, initialPos, goal, goalPos, "two edits different lines");
  });
}
