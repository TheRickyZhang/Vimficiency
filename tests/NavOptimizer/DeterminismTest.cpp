// tests/NavOptimizer/DeterminismTest.cpp
//
// Property: same input always produces same output.
// Verifies that optimizers are deterministic (no unordered_map iteration issues, etc.)
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="NavOptimizerDeterminismTests.*"

#include <gtest/gtest.h>

#include "Boundary/NavBoundary.h"
#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class NavOptimizerDeterminismTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext NavOptimizerDeterminismTests::navContext;

// =============================================================================
// NavOptimizer Determinism
// =============================================================================

TEST_F(NavOptimizerDeterminismTests, SameInputProducesSameOutput) {
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(44);

  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(2, 3), 10, 25);
    CursorPos start = randomPos(lines);
    CursorPos end = randomPos(lines);

    NavOptimizer opt1(config);
    NavOptimizer opt2(config);

    auto res1 = opt1.optimize(
      lines, start, end, {}, "jjjjj"
    );

    auto res2 = opt2.optimize(
      lines, start, end, {}, "jjjjj"
    );

    const auto& results1 = res1.getResults();
    const auto& results2 = res2.getResults();

    if (results1.size() != results2.size()) {
      failures++;
      if (failures <= 3) {
        cerr << "Iter " << iter << ": Different result counts: "
             << results1.size() << " vs " << results2.size() << endl;
      }
      continue;
    }

    for (size_t i = 0; i < results1.size(); i++) {
      if (results1[i].getSequence() != results2[i].getSequence() ||
          abs(results1[i].getCost() - results2[i].getCost()) > 1e-6) {
        failures++;
        if (failures <= 3) {
          cerr << "Iter " << iter << ": Result " << i << " differs\n"
               << "  Run 1: " << results1[i].getSequence() << "\n"
               << "  Run 2: " << results2[i].getSequence() << endl;
        }
        break;
      }
    }
  }

  EXPECT_EQ(failures, 0) << failures << "/" << NUM_ITERATIONS << " iterations had non-deterministic results";
}
