// tests/TransformOptimizer/DeterminismTest.cpp
//
// Property: same input always produces same output.
// Verifies that optimizers are deterministic (no unordered_map iteration issues, etc.)
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TransformOptimizerDeterminismTests.*"

#include <gtest/gtest.h>

#include "Boundary/TransformBoundary.h"
#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
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

class TransformOptimizerDeterminismTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext TransformOptimizerDeterminismTests::navContext;

// =============================================================================
// TransformOptimizer Determinism
// =============================================================================

TEST_F(TransformOptimizerDeterminismTests, SameInputProducesSameOutput) {
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(45);

  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(1, 2), 4, 8);
    TransformBoundary boundary(lines, {0, 0}, lines.endPos());

    TransformOptimizer opt1(config);
    TransformOptimizer opt2(config);

    TransformResult res1 = pureDeletionResult(opt1, lines, boundary);
    TransformResult res2 = pureDeletionResult(opt2, lines, boundary);

    if (res1.resultCount() != res2.resultCount()) {
      failures++;
      if (failures <= 3) {
        cerr << "Iter " << iter << ": Different result counts" << endl;
      }
      continue;
    }

    bool mismatch = false;
    for (size_t i = 0; i < res1.resultCount() && !mismatch; i++) {
      const auto& b1 = res1.getResults()[i];
      const auto& b2 = res2.getResults()[i];

      if (b1.empty() != b2.empty()) {
        mismatch = true;
      } else if (!b1.empty() && b1[0].getSequence() != b2[0].getSequence()) {
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
