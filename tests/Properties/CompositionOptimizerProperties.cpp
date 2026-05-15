#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OptimizerResultChecks.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

class CompositionOptimizerGeneratedPropertyTest {
 public:
  void SingleLineEditTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int lineLen = RandomGen::range(8, 20);
      string line = randomWord(lineLen);
      Lines initial = {line};

      int editStart = RandomGen::range(0, max(0, lineLen - 3));
      int editLen = RandomGen::range(1, min(5, lineLen - editStart));
      int editEnd = editStart + editLen;

      string replacement = randomWord(RandomGen::range(1, 6));
      Lines goal = {line.substr(0, editStart) + replacement + line.substr(editEnd)};

      CursorPos initialPos(0, RandomGen::range(0, max(0, lineLen - 1)));
      expectOptimizationTopResultsReachGoal(
          initial, initialPos, goal, CursorPos(0, 0), "single-line edit");
    });
  }

  void MultiLineSingleEditTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
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
      goal[editLine] =
          targetLine.substr(0, editStart) + replacement + targetLine.substr(editEnd);

      int cursorLine = RandomGen::range(0, numLines - 1);
      int cursorCol = RandomGen::range(
          0, max(0, static_cast<int>(initial[cursorLine].size()) - 1));
      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(cursorLine, cursorCol), goal, CursorPos(0, 0),
          "multi-line single edit");
    });
  }

  void PureInsertionTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int numLines = RandomGen::range(1, 3);
      Lines initial = randomLines(numLines, 4, 10);

      int insertLine = RandomGen::range(0, numLines - 1);
      int insertCol = RandomGen::range(0, static_cast<int>(initial[insertLine].size()));
      string insertText = randomWord(RandomGen::range(2, 6));

      Lines goal = initial;
      goal[insertLine] = initial[insertLine].substr(0, insertCol) +
                         insertText +
                         initial[insertLine].substr(insertCol);

      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(0, 0), goal, CursorPos(0, 0), "pure insertion");
    });
  }

  void PureDeletionTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int numLines = RandomGen::range(1, 3);
      Lines initial = randomLines(numLines, 8, 15);

      int deleteLine = RandomGen::range(0, numLines - 1);
      int lineLen = static_cast<int>(initial[deleteLine].size());
      if (lineLen < 4) return;

      int deleteStart = RandomGen::range(0, lineLen - 3);
      int deleteLen = RandomGen::range(2, min(6, lineLen - deleteStart));
      int deleteEnd = deleteStart + deleteLen;

      Lines goal = initial;
      goal[deleteLine] =
          initial[deleteLine].substr(0, deleteStart) +
          initial[deleteLine].substr(deleteEnd);

      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(0, 0), goal, CursorPos(0, 0), "pure deletion");
    });
  }

  void InsertNewLineTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int numLines = RandomGen::range(2, 3);
      Lines initial = randomLines(numLines, 4, 8);

      int insertAfter = RandomGen::range(0, numLines - 1);
      Lines goal = initial;
      goal.insert(goal.begin() + insertAfter + 1, randomWord(RandomGen::range(3, 7)));

      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(0, 0), goal, CursorPos(0, 0), "insert new line");
    });
  }

  void DeleteEntireLineTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int numLines = RandomGen::range(3, 4);
      Lines initial = randomLines(numLines, 4, 8);

      Lines goal = initial;
      goal.erase(goal.begin() + RandomGen::range(1, numLines - 2));

      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(0, 0), goal, CursorPos(0, 0), "delete entire line");
    });
  }

  void TwoEditsSameLineTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      string line = randomWord(3) + " " + randomWord(4) + " " + randomWord(3);
      Lines initial = {line};

      size_t firstSpace = line.find(' ');
      size_t lastSpace = line.rfind(' ');
      string middle = line.substr(firstSpace, lastSpace - firstSpace + 1);
      Lines goal = {randomWord(3) + middle + randomWord(3)};

      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(0, 0), goal, CursorPos(0, 0), "two edits same line");
    });
  }

  void TwoEditsDifferentLinesTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      Lines initial = randomLines(3, 5, 10);

      Lines goal = initial;
      goal[0] = randomWord(RandomGen::range(4, 8));
      goal[2] = randomWord(RandomGen::range(4, 8));

      expectOptimizationTopResultsReachGoal(
          initial, CursorPos(0, 0), goal, CursorPos(0, 0),
          "two edits different lines");
    });
  }

 private:
  static constexpr size_t kMaxResultsToReplay = 3;

  Config config_ = Config::uniform();
  CompositionOptimizer opt_{config_};
  CompositionOptimizerParams params_{};
  NeovimOracle oracle_{};

  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }

  void expectTopResultsReachGoal(
      const vector<Result>& results,
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      const string& testContext) {
    OptimizerResultChecks::expectTopResultsReplay(
        oracle_, results, initial, initialPos, goal,
        kMaxResultsToReplay, testContext);
  }

  void expectOptimizationTopResultsReachGoal(
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      CursorPos goalPos,
      const string& testContext) {
    auto compResult = opt_.optimize(initial, initialPos, goal, goalPos, params_);
    expectTopResultsReachGoal(
        compResult.getResults(), initial, initialPos, goal, testContext);
  }
};

}  // namespace

FUZZ_TEST_F(CompositionOptimizerGeneratedPropertyTest, SingleLineEditTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({42});

FUZZ_TEST_F(
    CompositionOptimizerGeneratedPropertyTest, MultiLineSingleEditTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({43});

FUZZ_TEST_F(CompositionOptimizerGeneratedPropertyTest, PureInsertionTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({44});

FUZZ_TEST_F(CompositionOptimizerGeneratedPropertyTest, PureDeletionTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({45});

FUZZ_TEST_F(CompositionOptimizerGeneratedPropertyTest, InsertNewLineTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({46});

FUZZ_TEST_F(CompositionOptimizerGeneratedPropertyTest, DeleteEntireLineTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({47});

FUZZ_TEST_F(CompositionOptimizerGeneratedPropertyTest, TwoEditsSameLineTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({48});

FUZZ_TEST_F(
    CompositionOptimizerGeneratedPropertyTest, TwoEditsDifferentLinesTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({49});
