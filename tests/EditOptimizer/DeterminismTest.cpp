// tests/Optimizer/DeterminismTests.cpp
//
// Property: same input always produces same output
// Verifies that optimizers are deterministic (no unordered_map iteration issues, etc.)

#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/Lines.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class EditOptimizerDeterminismTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext EditOptimizerDeterminismTests::navContext;

// =============================================================================
// EditOptimizer Determinism
// =============================================================================

TEST_F(EditOptimizerDeterminismTests, SameInputProducesSameOutput) {
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(45);

  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(1, 2), 4, 8);
    EditBoundary boundary(lines, {0, 0}, lines.lastPos());

    EditOptimizer opt1(config, OptimizerParams{});
    EditOptimizer opt2(config, OptimizerParams{});

    EditResult res1 = opt1.optimizeEdit(lines, {""}, boundary);
    EditResult res2 = opt2.optimizeEdit(lines, {""}, boundary);

    if (res1.typeAllResults.size() != res2.typeAllResults.size()) {
      failures++;
      if (failures <= 3) {
        cerr << "Iter " << iter << ": Different result counts" << endl;
      }
      continue;
    }

    bool mismatch = false;
    for (size_t i = 0; i < res1.typeAllResults.size() && !mismatch; i++) {
      const auto& r1 = res1.typeAllResults[i];
      const auto& r2 = res2.typeAllResults[i];

      if (r1.isValid() != r2.isValid()) {
        mismatch = true;
      } else if (r1.isValid() && r1.getSequenceString() != r2.getSequenceString()) {
        mismatch = true;
      }
    }

    if (mismatch) {
      failures++;
      if (failures <= 3) {
        cerr << "Iter " << iter << ": Results differ between runs" << endl;
      }
    }
  }

  EXPECT_EQ(failures, 0) << failures << "/" << NUM_ITERATIONS << " iterations had non-deterministic results";
}
