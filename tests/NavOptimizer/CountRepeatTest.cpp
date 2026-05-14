#include "NavOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(NavOptimizer_ManualTest, MinCountRepeat_BlocksSmallCounts) {
  // "one two three four five six" — 4w reaches "five" (col 20)
  Lines lines = {"one two three four five six"};
  CursorPos start(0, 0);
  CursorPos end(0, 14); // "four" — reachable by 3w

  NavOptimizer opt(Config::uniform());
  NavBoundary boundary;

  // With default minCountRepeat=4, count=3 should NOT appear as "3w"
  auto results = opt.optimize(lines, start, end,
      NavOptimizerParams{}
          .withMaxResults(30)
          .withMaxNodesPopped(20000)
          .withMaxResultsPerEndPos(2),
      "", boundary, navContext).getResults();

  EXPECT_FALSE(hasSequence(results, "3w")) << "3w should be blocked by minCountRepeat=4";
  EXPECT_FALSE(hasSequence(results, "3W")) << "3W should be blocked by minCountRepeat=4";
}

TEST_F(NavOptimizer_ManualTest, MinCountRepeat_LowThresholdAllowsSmallCounts) {
  Lines lines = {"one two three four five six"};
  CursorPos start(0, 0);
  CursorPos end(0, 14); // "four" — reachable by 3w

  NavOptimizer opt(Config::uniform());
  NavBoundary boundary;

  // With minCountRepeat=2, count=3 SHOULD appear
  auto results = opt.optimize(lines, start, end,
      NavOptimizerParams{}.withMaxResults(30).withMaxNodesPopped(20000)
          .withMinCountRepeat(2)
          .withMaxResultsPerEndPos(2),
      "", boundary, navContext).getResults();

  EXPECT_TRUE(hasSequence(results, "3w")) << "3w should be allowed with minCountRepeat=2";
}

}  // namespace
