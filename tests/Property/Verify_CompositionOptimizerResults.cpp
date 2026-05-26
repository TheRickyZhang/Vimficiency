// Property: for generated before/after buffers, CompositionOptimizer top results
// must replay in Neovim from the generated initial cursor to the exact generated
// goal buffer and goal cursor.

#include <cstdint>
#include <sstream>
#include <string>

#include <fuzztest/fuzztest.h>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Property/OptimizerResultChecks.h"
#include "Property/PropertyTestUtils.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

struct CompositionReplayCase {
  Lines initial;
  CursorPos initialPos;
  Lines goal;
  CursorPos goalPos;
};

void replaceLineSpan(Lines& lines) {
  int line = randomLineIndex(lines);
  string& text = lines[line];
  int begin = randomInsertCol(text);
  int end = RandomGen::range(begin, static_cast<int>(text.size()));
  text.replace(begin, end - begin, randomLine(RandomGen::range(0, 6)));
}

void splitLine(Lines& lines) {
  int line = randomLineIndex(lines);
  string& text = lines[line];
  int col = randomInsertCol(text);
  string suffix = text.substr(col);
  text.erase(col);
  lines.insert(lines.begin() + line + 1, suffix);
}

void joinAdjacentLines(Lines& lines) {
  if (lines.size() < 2) {
    replaceLineSpan(lines);
    return;
  }

  int line = RandomGen::range(0, lines.lastLine() - 1);
  lines[line] += lines[line + 1];
  lines.erase(lines.begin() + line + 1);
}

void insertLine(Lines& lines) {
  int index = RandomGen::range(0, static_cast<int>(lines.size()));
  lines.insert(lines.begin() + index, randomLine(RandomGen::range(0, 8)));
}

void deleteLine(Lines& lines) {
  if (lines.size() < 2) {
    replaceLineSpan(lines);
    return;
  }

  lines.erase(lines.begin() + randomLineIndex(lines));
}

void applyRandomMutation(Lines& lines) {
  switch (RandomGen::range(0, 4)) {
    case 0:
      replaceLineSpan(lines);
      break;
    case 1:
      splitLine(lines);
      break;
    case 2:
      joinAdjacentLines(lines);
      break;
    case 3:
      insertLine(lines);
      break;
    default:
      deleteLine(lines);
      break;
  }
}

CompositionReplayCase generateReplayCase() {
  CompositionReplayCase test;
  test.initial = randomLines(RandomGen::range(1, 4), 3, 9);
  test.goal = test.initial;

  // Mix simple text edits, line splits/joins, and line insert/delete instead
  // of encoding each shape as a separate property entry point.
  int mutationCount = RandomGen::range(1, 3);
  for (int i = 0; i < mutationCount; i++) {
    applyRandomMutation(test.goal);
  }

  if (test.goal == test.initial) {
    test.goal[0] += "a";
  }

  test.initialPos = randomPos(test.initial);
  test.goalPos = randomPos(test.goal);
  return test;
}

class CompositionOptimizerGeneratedPropertyTest {
 public:
  void RandomBufferMutationsTopResultsReplay(uint32_t seed) {
    runSeedDriverCases(seed, 80, [&] {
      CompositionReplayCase test = generateReplayCase();
      auto compResult = opt_.optimize(
          test.initial, test.initialPos, test.goal, test.goalPos, params_);

      ostringstream context;
      context << "random composition mutation initial=" << test.initial
              << " initialPos=" << test.initialPos
              << " goal=" << test.goal
              << " goalPos=" << test.goalPos;
      expectTopResultsReplay(
          oracle_, compResult.getResults(), test.initial, test.initialPos,
          test.goal, MAX_RESULTS_TO_REPLAY, context.str(), test.goalPos);
    });
  }

 private:
  static constexpr size_t MAX_RESULTS_TO_REPLAY = 3;

  Config config_ = Config::uniform();
  CompositionOptimizer opt_{config_};
  CompositionOptimizerParams params_{};
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(
    CompositionOptimizerGeneratedPropertyTest, RandomBufferMutationsTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));
