// tests/NavOptimizer/CostConsistencyTest.cpp
//
// Verifies that reported costs match independently computed costs.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="NavOptimizerCostConsistencyTests.*"

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

class NavOptimizerCostConsistencyTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext NavOptimizerCostConsistencyTests::navContext;

// =============================================================================
// NavOptimizer Cost Consistency
// =============================================================================

TEST_F(NavOptimizerCostConsistencyTests, CostMatchesComputed) {
  const int NUM_ITERATIONS = 50;
  RandomGen::seed(42);
  NavOptimizer opt(config);

  int totalResults = 0;
  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(2, 4), 8, 25);
    CursorPos start = randomPosition(lines);
    CursorPos end = randomPosition(lines);

    auto results = opt.optimize(
      lines, start, end, NavOptimizerParams{}.withMaxResults(5), "jjjjj"
    ).getResults();

    for (const auto& result : results) {
      if (!result.isValid()) continue;
      totalResults++;

      const auto& seq = result.getSequence();
      double computedCost = getEffort(seq.view(), config);
      double reportedCost = result.getCost();

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
