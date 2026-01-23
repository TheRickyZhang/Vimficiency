// tests/Optimizer/CostConsistencyTests.cpp
//
// Property: result.keyCost == getEffort(result.getSequenceString(), config)
// Verifies that reported costs match independently computed costs.

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

class CostConsistencyTests : public ::testing::Test {
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

NavContext CostConsistencyTests::navContext;

// =============================================================================
// MovementOptimizer Cost Consistency
// =============================================================================

TEST_F(CostConsistencyTests, MovementOptimizer) {
  const int NUM_ITERATIONS = 50;
  mt19937 rng(42);
  MovementOptimizer opt(config);

  int totalResults = 0;
  int failures = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(rng, 2 + rng() % 3, 8, 25);
    Position start = randomPosition(rng, lines);
    Position end = randomPosition(rng, lines);

    auto results = opt.optimize(
      lines, start, RunningEffort(), end, "jjjjj", navContext,
      MotionBoundary(), EXPLORABLE_MOTIONS, OptimizerParams(5, 1e4, 1.0, 2.0)
    );

    for (const auto& result : results) {
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

// =============================================================================
// EditOptimizer Cost Consistency
// =============================================================================

TEST_F(CostConsistencyTests, EditOptimizer) {
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
