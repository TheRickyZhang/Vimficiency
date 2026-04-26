// tests/TransformOptimizer/CostConsistencyTest.cpp
//
// Verifies that reported costs match independently computed costs.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TransformOptimizerCostConsistencyTests.*"

#include <gtest/gtest.h>

#include "Boundary/TransformBoundary.h"
#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Effort/RunningEffort.h"
#include "Utils/EditTestGenerators.h"
#include "Types/Lines.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {
TransformResult pureDeletionResult(
    TransformOptimizer& opt,
    const Lines& initialLines,
    TransformBoundary boundary) {
  return opt.optimizePureDeletion(initialLines, boundary);
}
} // namespace

// =============================================================================
// Test Infrastructure
// =============================================================================

class TransformOptimizerCostConsistencyTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext TransformOptimizerCostConsistencyTests::navContext;


TEST_F(TransformOptimizerCostConsistencyTests, CostMatchesComputed) {
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(43);

  int totalResults = 0;
  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(1, 2), 4, 10);
    TransformBoundary boundary(lines, {0, 0}, lines.endPos());
    TransformOptimizer opt(config);

    TransformResult res = pureDeletionResult(opt, lines, boundary);

    for (const auto& bucket : res.getResults()) {
      if (bucket.empty()) continue;
      const auto& result = bucket[0];
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
