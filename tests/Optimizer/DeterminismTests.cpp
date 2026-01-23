// tests/Optimizer/DeterminismTests.cpp
//
// Property: same input always produces same output
// Verifies that optimizers are deterministic (no unordered_map iteration issues, etc.)

#include <gtest/gtest.h>
#include <random>

#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/MovementOptimizer.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "Boundary/EditBoundary.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"
#include "Utils/EditTestGenerators.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class DeterminismTests : public ::testing::Test {
protected:
  Config config = Config::uniform();
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  static Position randomPosition(mt19937& rng, const Lines& lines) {
    uniform_int_distribution<int> lineDist(0, static_cast<int>(lines.size()) - 1);
    int line = lineDist(rng);
    int col = lines[line].empty() ? 0 :
      static_cast<int>(rng() % lines[line].size());
    return Position(line, col);
  }
};

NavContext DeterminismTests::navContext;

// =============================================================================
// MovementOptimizer Determinism
// =============================================================================

TEST_F(DeterminismTests, MovementOptimizer) {
  const int NUM_ITERATIONS = 30;
  mt19937 rng(44);

  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(rng, 2 + rng() % 2, 10, 25);
    Position start = randomPosition(rng, lines);
    Position end = randomPosition(rng, lines);

    MovementOptimizer opt1(config);
    MovementOptimizer opt2(config);

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

// =============================================================================
// EditOptimizer Determinism
// =============================================================================

TEST_F(DeterminismTests, EditOptimizer) {
  const int NUM_ITERATIONS = 30;
  mt19937 rng(45);

  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(rng, 1 + rng() % 2, 4, 8);
    EditBoundary boundary(lines, {0, 0}, lines.lastPos());

    EditOptimizer opt1(config, OptimizerParams(10, 1e4, 1.0, 2.0));
    EditOptimizer opt2(config, OptimizerParams(10, 1e4, 1.0, 2.0));

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
