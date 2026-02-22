// tests/EditOptimizer/DeterminismTest.cpp
//
// Property: same input always produces same output.
// Verifies that optimizers are deterministic (no unordered_map iteration issues, etc.)
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="EditOptimizerDeterminismTests.*"

#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
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
    EditBoundary boundary(lines, {0, 0}, lines.endPos());

    EditOptimizer opt1(config);
    EditOptimizer opt2(config);

    EditResult res1 = pureDeletionResult(opt1, lines, boundary);
    EditResult res2 = pureDeletionResult(opt2, lines, boundary);

    if (res1.resultCount() != res2.resultCount()) {
      failures++;
      if (failures <= 3) {
        cerr << "Iter " << iter << ": Different result counts" << endl;
      }
      continue;
    }

    bool mismatch = false;
    for (size_t i = 0; i < res1.resultCount() && !mismatch; i++) {
      const auto& r1 = res1.getResults()[i];
      const auto& r2 = res2.getResults()[i];

      if (r1.isValid() != r2.isValid()) {
        mismatch = true;
      } else if (r1.isValid() && r1.sequence != r2.sequence) {
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
