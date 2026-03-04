// tests/MotionOptimizer/StartingEffortTradeoffTest.cpp
//
// Documents the benchmark that justified removing startingEffort from the
// MotionOptimizer A* search. The original test compared top-N sequences found
// with vs without prior effort context and measured:
//
//   allowMultiple=false: Jaccard=1.000, perfect overlap=100%, best-in-top-N=100%
//   allowMultiple=true:  Jaccard=0.988, perfect overlap=97%,  best-in-top-N=100%
//
// Conclusion: startingEffort does not meaningfully affect which sequences are
// discovered, so it was removed from the public API.
//
// This test now serves as a smoke test verifying optimizeToRange works correctly
// across random scenarios without startingEffort.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="StartingEffortTradeoffTest.*"

#include <gtest/gtest.h>

#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class StartingEffortTradeoffTest : public ::testing::Test {
protected:
  Config config = Config::qwerty();

  struct Scenario {
    Lines lines;
    CursorPos initialPos;
    CursorPos rangeBegin;
    CursorPos rangeEnd;
  };

  // Generate a random scenario where initialPos is outside [rangeBegin, rangeEnd)
  Scenario generateScenario() {
    int numLines = RandomGen::range(10, 30);
    Lines lines = randomCodeBuffer(numLines, 40);

    for (int attempt = 0; attempt < 100; attempt++) {
      CursorPos a = randomPosition(lines);
      CursorPos b = randomPosition(lines);
      CursorPos c = randomPosition(lines);

      CursorPos rangeBegin = (a < b) ? a : b;
      CursorPos rangeEnd = (a < b) ? b : a;

      if (rangeBegin.line == rangeEnd.line && rangeBegin.col == rangeEnd.col) continue;

      if (c < rangeBegin || !(c < rangeEnd)) {
        return { lines, c, rangeBegin, rangeEnd };
      }
    }

    CursorPos start(0, 0);
    int lastLine = static_cast<int>(lines.size()) - 1;
    CursorPos rb(lastLine - 1, 0);
    CursorPos re(lastLine, lines[lastLine].empty() ? 0 : static_cast<int>(lines[lastLine].size()) - 1);
    return { lines, start, rb, re };
  }
};

// =============================================================================
// Smoke test: optimizeToRange produces results across random scenarios
// =============================================================================

TEST_F(StartingEffortTradeoffTest, SmokeTest) {
  const int NUM_ITERATIONS = 200;
  RandomGen::seed(42);
  MotionOptimizer opt(config);

  int totalResults = 0;
  int validScenarios = 0;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    auto scenario = generateScenario();

    auto params = MotionOptimizerRangeParams{}.withMaxResults(5);

    auto result = opt.optimizeToRange(
      scenario.lines, scenario.initialPos,
      scenario.lines.inclusiveRangeFromHalfOpen(
          scenario.rangeBegin, scenario.rangeEnd),
      params
    );

    if (!result.getResults().empty()) {
      validScenarios++;
      totalResults += static_cast<int>(result.getResults().size());
    }
  }

  // cerr << "StartingEffortTradeoffTest smoke: " << validScenarios << "/"
  //      << NUM_ITERATIONS << " scenarios produced results, "
  //      << totalResults << " total results" << endl;

  EXPECT_GT(validScenarios, NUM_ITERATIONS / 2)
      << "Expected most scenarios to produce results";
}
