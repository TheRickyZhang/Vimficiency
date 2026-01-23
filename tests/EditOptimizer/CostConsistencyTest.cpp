// Verifies that reported costs match independently computed costs.

#include <gtest/gtest.h>
#include <random>

#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"
#include "Utils/EditTestGenerators.h"

using namespace std;

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
  mt19937 rng(43);

  int totalResults = 0;
  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(rng, 1 + rng() % 2, 4, 10);
    EditBoundary boundary(lines, {0, 0}, lines.lastPos());
    EditOptimizer opt(config, OptimizerParams(10, 1e4, 1.0, 2.0));

    EditResult res = opt.optimizeEdit(lines, {""}, boundary);

    for (const auto& result : res.typeAllResults) {
      if (!result.isValid()) continue;
      totalResults++;

      string seq = result.getSequenceString();
      double computedCost = getEffort(seq, config);
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
