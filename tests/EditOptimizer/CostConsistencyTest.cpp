// tests/EditOptimizer/CostConsistencyTest.cpp
//
// Verifies that reported costs match independently computed costs.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="EditOptimizerCostConsistencyTests.*"

#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "State/RunningEffort.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/Lines.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {
EditResult pureDeletionResult(
    EditOptimizer& opt,
    const Lines& initialLines,
    EditBoundary boundary) {
  return opt.optimizePureDeletion(initialLines, boundary).editResult;
}
} // namespace

// =============================================================================
// Test Infrastructure
// =============================================================================

class EditOptimizerCostConsistencyTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext EditOptimizerCostConsistencyTests::navContext;


TEST_F(EditOptimizerCostConsistencyTests, CostMatchesComputed) {
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(43);

  int totalResults = 0;
  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(1, 2), 4, 10);
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    EditOptimizer opt(config);

    EditResult res = pureDeletionResult(opt, lines, boundary);

    for (const auto& result : res.getResults()) {
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
