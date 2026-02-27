// tests/EditOptimizer/MultipleResultsPerStartTest.cpp
//
// Coverage for EditOptimizerParams::maxMultiplePerStartPosition.

#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Types/Lines.h"

using namespace std;

namespace {

EditBoundary fullBoundary(const Lines& lines) {
  return EditBoundary(lines, CursorPos(0, 0), lines.endPos());
}

} // namespace

class EditOptimizerMultipleResultsTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  EditOptimizer optimizer{config};
};

TEST_F(EditOptimizerMultipleResultsTest, DefaultKeepsSingleResultPerStart) {
  Lines lines = {"a"};

  EditResult result = optimizer.optimizePureDeletion(
      lines, fullBoundary(lines),
      EditOptimizerParams{}
          .withMaxResults(50)
          .withMaxNodesExplored(100000));

  const Result* best = result.resultAt(0, 0);
  ASSERT_NE(best, nullptr);
  EXPECT_EQ(result.resultsCountAt(0, 0), 1u);

  auto all = result.resultsAt(0, 0);
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(best->getSequence(), all[0].getSequence());
  EXPECT_DOUBLE_EQ(best->getCost(), all[0].getCost());
}

TEST_F(EditOptimizerMultipleResultsTest, SupportsMultipleResultsPerStartWhenRequested) {
  Lines lines = {"a"};

  EditResult result = optimizer.optimizePureDeletion(
      lines, fullBoundary(lines),
      EditOptimizerParams{}
          .withMaxResults(50)
          .withMaxNodesExplored(100000)
          .withMaxMultiplePerStartPosition(3));

  auto all = result.resultsAt(0, 0);
  EXPECT_GE(all.size(), 2u);
  EXPECT_LE(all.size(), 3u);

  const Result* best = result.resultAt(0, 0);
  ASSERT_NE(best, nullptr);
  ASSERT_FALSE(all.empty());
  EXPECT_EQ(best->getSequence(), all[0].getSequence());
  EXPECT_DOUBLE_EQ(best->getCost(), all[0].getCost());
}

TEST_F(EditOptimizerMultipleResultsTest, MaxMultiplePerStartPositionLessThanOneClampsToOne) {
  Lines lines = {"a"};

  EditResult result = optimizer.optimizePureDeletion(
      lines, fullBoundary(lines),
      EditOptimizerParams{}
          .withMaxResults(50)
          .withMaxNodesExplored(100000)
          .withMaxMultiplePerStartPosition(0));

  EXPECT_EQ(result.resultsCountAt(0, 0), 1u);
}

TEST_F(EditOptimizerMultipleResultsTest, GlobalMaxResultsStillCapsTotalEmittedResults) {
  Lines lines = {"a"};

  EditResult result = optimizer.optimizePureDeletion(
      lines, fullBoundary(lines),
      EditOptimizerParams{}
          .withMaxResults(2)
          .withMaxNodesExplored(100000)
          .withMaxMultiplePerStartPosition(3));

  EXPECT_LE(result.resultsCountAt(0, 0), 2u);
  EXPECT_LE(result.getStats().resultsFound, 2);
}
