// tests/MotionOptimizer/CostConsistencyTest.cpp
//
// Verifies that reported costs match independently computed costs.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="MotionOptimizerCostConsistencyTests.*"

#include <gtest/gtest.h>

#include "Boundary/MotionBoundary.h"
#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class MotionOptimizerCostConsistencyTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext MotionOptimizerCostConsistencyTests::navContext;

// =============================================================================
// MotionOptimizer Cost Consistency
// =============================================================================

TEST_F(MotionOptimizerCostConsistencyTests, CostMatchesComputed) {
  const int NUM_ITERATIONS = 50;
  RandomGen::seed(42);
  MotionOptimizer opt(config);

  int totalResults = 0;
  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(2, 4), 8, 25);
    CursorPos start = randomPosition(lines);
    CursorPos end = randomPosition(lines);

    auto results = opt.optimize(
      lines, start, end, MotionOptimizerParams{}.withMaxResults(5), "jjjjj"
    ).results;

    for (const auto& result : results) {
      if (!result.isValid()) continue;
      totalResults++;

      const auto& seq = result.getSequenceString();
      double computedCost = getEffort(seq.view(), config);
      double reportedCost = result.keyCost;

      if (abs(computedCost - reportedCost) > 1e-6) {
        failures++;
        if (failures <= 3) {
          cerr << "Iter " << iter << ": Cost mismatch for '" << seq << "'\n"
               << "  Computed: " << computedCost << ", Reported: " << reportedCost << endl;
        }
      }
    }
  }

  EXPECT_EQ(failures, 0) << failures << "/" << totalResults << " results had cost mismatches";
}
