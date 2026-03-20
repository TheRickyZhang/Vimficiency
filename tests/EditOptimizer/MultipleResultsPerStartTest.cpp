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

vector<size_t> collectPerStartCounts(const EditResult& result, const Lines& lines) {
  vector<size_t> counts;
  for (int line = 0; line < static_cast<int>(lines.size()); line++) {
    for (int col = 0; col < lines[line].effectiveSize(); col++) {
      counts.push_back(result.resultsAt(line, col).size());
    }
  }
  return counts;
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
          .withMaxNodesPopped(100000));

  const Result* best = result.resultAt(0, 0);
  ASSERT_NE(best, nullptr);
  EXPECT_EQ(result.resultsAt(0, 0).size(), 1u);

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
          .withMaxNodesPopped(100000)
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
          .withMaxNodesPopped(100000)
          .withMaxMultiplePerStartPosition(0));

  EXPECT_EQ(result.resultsAt(0, 0).size(), 1u);
}

TEST_F(EditOptimizerMultipleResultsTest, GlobalMaxResultsStillCapsTotalEmittedResults) {
  Lines lines = {"a"};

  EditResult result = optimizer.optimizePureDeletion(
      lines, fullBoundary(lines),
      EditOptimizerParams{}
          .withMaxResults(2)
          .withMaxNodesPopped(100000)
          .withMaxMultiplePerStartPosition(3));

  EXPECT_LE(result.resultsAt(0, 0).size(), 2u);
  EXPECT_LE(result.getStats().resultsFound(), 2);
}

TEST_F(EditOptimizerMultipleResultsTest, HighMaxResultsValuesDoNotIncreaseWorkAfterTerminalStarts) {
  const int perStartCap = 4;
  const int maxPops = 200000;
  vector<Lines> candidates = {
      {"ab"},
      {"abc"},
      {"abcd"},
      {"a", "b"},
      {"ab", "c"},
      {"abc", "d"},
      {"a", "bc"},
      {"ab", "cd"},
      {"abc", "de"},
      {"a", "", "b"},
      {"ab", "", "cd"},
      {"abc", "", "def"}
  };

  bool validated = false;

  auto runCase = [&](const Lines& lines, int maxResults) {
    return optimizer.optimizePureDeletion(
        lines, fullBoundary(lines),
        EditOptimizerParams{}
            .withMaxResults(maxResults)
            .withMaxNodesPopped(maxPops)
            .withMaxMultiplePerStartPosition(perStartCap));
  };

  for (const Lines& lines : candidates) {
    EditResult runA = runCase(lines, 1000);
    vector<size_t> countsA = collectPerStartCounts(runA, lines);

    bool hasCapped = false;
    bool hasUncapped = false;
    for (size_t c : countsA) {
      hasCapped |= (c >= static_cast<size_t>(perStartCap));
      hasUncapped |= (c > 0 && c < static_cast<size_t>(perStartCap));
    }
    if (!(hasCapped && hasUncapped)) continue;

    EditResult runB = runCase(lines, 5000);
    vector<size_t> countsB = collectPerStartCounts(runB, lines);

    const auto& statsA = runA.getStats();
    const auto& statsB = runB.getStats();
    if (statsA.stopReason() != SearchStopReason::AllResultsFound ||
        statsB.stopReason() != SearchStopReason::AllResultsFound) {
      continue;
    }

    SCOPED_TRACE("line_count=" + to_string(lines.size()));
    EXPECT_EQ(countsA, countsB);
    EXPECT_EQ(statsA.resultsFound(), statsB.resultsFound());
    EXPECT_EQ(statsA.nodesExplored(), statsB.nodesExplored());
    validated = true;
    break;
  }

  EXPECT_TRUE(validated)
      << "No candidate produced mixed capped/uncapped starts with terminal-state stopping";
}
