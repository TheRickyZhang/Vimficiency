
// tests/Optimizer/DeterminismTests.cpp
//
// Property: same input always produces same output
// Verifies that optimizers are deterministic (no unordered_map iteration issues, etc.)

#include <gtest/gtest.h>
#include <random>

#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/MotionOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"
#include "Utils/EditTestGenerators.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class MotionOptimizerDeterminismTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }
};

NavContext MotionOptimizerDeterminismTests::navContext;

// =============================================================================
// MotionOptimizer Determinism
// =============================================================================

TEST_F(MotionOptimizerDeterminismTests, SameInputProducesSameOutput) {
  const int NUM_ITERATIONS = 30;
  mt19937 rng(44);

  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(rng, 2 + rng() % 2, 10, 25);
    Position start = randomPosition(rng, lines);
    Position end = randomPosition(rng, lines);

    MotionOptimizer opt1(config);
    MotionOptimizer opt2(config);

    auto results1 = opt1.optimize(
      lines, start, RunningEffort(), end, "jjjjj", navContext,
      MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
    );

    auto results2 = opt2.optimize(
      lines, start, RunningEffort(), end, "jjjjj", navContext,
      MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(10, 1e4, 1.0, 2.0)
    );

    if (results1.size() != results2.size()) {
      failures++;
      if (failures <= 3) {
        cerr << "Iter " << iter << ": Different result counts: "
             << results1.size() << " vs " << results2.size() << endl;
      }
      continue;
    }

    for (size_t i = 0; i < results1.size(); i++) {
      if (results1[i].getSequenceString() != results2[i].getSequenceString() ||
          abs(results1[i].keyCost - results2[i].keyCost) > 1e-6) {
        failures++;
        if (failures <= 3) {
          cerr << "Iter " << iter << ": Result " << i << " differs\n"
               << "  Run 1: " << results1[i].getSequenceString() << "\n"
               << "  Run 2: " << results2[i].getSequenceString() << endl;
        }
        break;
      }
    }
  }

  EXPECT_EQ(failures, 0) << failures << "/" << NUM_ITERATIONS << " iterations had non-deterministic results";
}
