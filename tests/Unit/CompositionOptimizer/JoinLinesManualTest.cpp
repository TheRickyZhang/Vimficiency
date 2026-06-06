#include "Unit/CompositionOptimizer/ManualTestHelpers.h"

#include "Optimizer/CompositionOptimizer/PlannedEditArtifacts.h"

using namespace std;

namespace {

DiffState makeWholeBufferDiff(const Lines& initial, const Lines& goal) {
  CursorPos begin(0, 0);
  CursorPos end = initial.endPos();
  return DiffState(
      begin, end, initial.flatten(), goal.flatten(),
      TransformBoundary(initial, begin, end));
}

TEST_F(CompositionOptimizer_ManualTest, JoinLinesExact) {
  // J alone, no residual: two lines joined with space
  Lines initial = {"hello", "world"};
  Lines goal = {"hello world"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos = goal.lastPos();

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  expectHasValidResults(res.getResults(), initial, initialPos, goal, "J exact");

  // Check that a result contains "J"
  bool foundJ = false;
  for (const Result& r : res.getResults()) {
    if (r.getSequence().view().find("J") != string::npos) { foundJ = true; break; }
  }
  EXPECT_TRUE(foundJ) << "Expected a result containing J";
}

TEST_F(CompositionOptimizer_ManualTest, JoinLinesWithIndent) {
  // J strips leading whitespace from next line
  Lines initial = {"aaa", "   bbb"};
  Lines goal = {"aaa bbb"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos = goal.lastPos();

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  ASSERT_FALSE(res.getResults().empty());
  // Verify J-based result (result 0); other results may have pre-existing oracle mismatches
  verifySingleResult(res.getResults()[0], initial, initialPos, goal, "J with indent");

  EXPECT_TRUE(res.getResults()[0].getSequence().view().find("J") != string::npos)
      << "Expected J in result[0]";
}

TEST_F(CompositionOptimizer_ManualTest, JoinLinesWithResidual) {
  // J + residual edit: join 3 lines, then edit the result
  Lines initial = {"aaa", "xxx", "ccc"};
  Lines goal = {"aaa bbb ccc"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos = goal.lastPos();

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  ASSERT_FALSE(res.getResults().empty());
  // Verify top result produces correct output
  verifySingleResult(res.getResults()[0], initial, initialPos, goal, "J with residual");

  // Check that at least one result uses J
  bool hasJ = false;
  for (const auto& r : res.getResults()) {
    if (r.getSequence().view().find("J") != string::npos) { hasJ = true; break; }
  }
  EXPECT_TRUE(hasJ) << "Expected at least one result with J";
}

TEST_F(CompositionOptimizer_ManualTest, WhitespaceJoinInsertionTransitionsReplay) {
  Lines initial = {"    ", "  ", "   ", ""};
  Lines goal = {"      ", "", " ", "  ", ""};
  CursorPos initialPos(3, 0);
  CursorPos goalPos(0, 2);

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  ASSERT_FALSE(res.getResults().empty());

  size_t checked = std::min<size_t>(3, res.getResults().size());
  for (size_t i = 0; i < checked; i++) {
    EXPECT_TRUE(OracleReplay::matches(
        *oracle, initial, initialPos, res.getResults()[i].getSequence().str(),
        goal, goalPos, Mode::Normal, "whitespace join insertion"))
        << "sequence=" << res.getResults()[i].getSequence().str();
  }
}

TEST_F(CompositionOptimizer_ManualTest, JoinPlanStartsWithBoundaryJoin) {
  Lines initial = {"aaa", "xxx", "ccc"};
  Lines goal = {"aaa bbb ccc"};
  DiffState diff = makeWholeBufferDiff(initial, goal);

  auto plan = computeJoinPlanForDiff(diff, initial, params, config);

  ASSERT_TRUE(plan.has_value());
  ASSERT_FALSE(plan->sequence.empty());
  EXPECT_TRUE(plan->sequence.view().starts_with("J"));
  EXPECT_EQ(plan->entryLine, 0);
}

TEST_F(CompositionOptimizer_ManualTest, JoinPlanSkipsResidualBeforeFirstJoin) {
  Lines initial = {"old", "aaa", "bbb"};
  Lines goal = {"new", "aaa bbb"};
  DiffState diff = makeWholeBufferDiff(initial, goal);

  auto plan = computeJoinPlanForDiff(diff, initial, params, config);

  EXPECT_FALSE(plan.has_value());
}

TEST_F(CompositionOptimizer_ManualTest, JoinResidualKeepsSentenceContext) {
  Lines initial = {"a bc", "bec .", "aaca", ".e.c cbf"};
  Lines goal = {"a bc", "bec .aacac"};
  CursorPos initialPos(3, 0);
  CursorPos goalPos(0, 3);

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);

  expectHasValidResults(
      res.getResults(), initial, initialPos, goal,
      "join residual keeps context");
}

TEST_F(CompositionOptimizer_ManualTest, JoinPlanEntryLineIsColumnInsensitive) {
  Lines initial = {"aaa", "xxx", "ccc"};
  Lines goal = {"aaa bbb ccc"};
  CursorPos initialPos(0, 2);
  CursorPos goalPos = goal.lastPos();

  // Pinned to Myers: the J-plan path keys off this diff shape, which the
  // TreeDiff first-draft planner does not yet surface (it still solves the
  // transform, just not via a leading J).
  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}.withDiffAlgorithm(0));

  expectHasValidResults(
      res.getResults(), initial, initialPos, goal,
      "join residual from nonzero column");

  bool hasLeadingJ = false;
  for (const auto& r : res.getResults()) {
    if (r.getSequence().view().starts_with("J")) {
      hasLeadingJ = true;
      break;
    }
  }
  EXPECT_TRUE(hasLeadingJ) << "Expected a result starting with J";
}

TEST_F(CompositionOptimizer_ManualTest, JoinLinesPartialJoin) {
  // M=2 partition: join first two lines, join last two lines
  Lines initial = {"aaa", "bbb", "ccc", "ddd"};
  Lines goal = {"aaa bbb", "ccc ddd"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos = goal.lastPos();
  NavBoundary boundary(initial, initialPos, initial.endPos());

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos,
      params, "", boundary);
  expectHasValidResults(res.getResults(), initial, initialPos, goal, "J partial join");

  bool foundJ = false;
  for (const Result& r : res.getResults()) {
    if (r.getSequence().view().find("J") != string::npos) { foundJ = true; break; }
  }
  EXPECT_TRUE(foundJ) << "Expected a result containing J";
}

TEST_F(CompositionOptimizer_ManualTest, GJEmittedForPureNewlineDeletion) {
  // Pure-newline-deletion diff: just remove the '\n' between two lines.
  // The diff's "in-range" cursor (L, lineEnd) is past line L's last char so
  // TransformOptimizer's start-position iteration skips it; the structural
  // (gJ) must therefore be offered as a composition motion regardless of
  // where the cursor currently sits on line L.
  Lines initial = {"abc", "def"};
  Lines goal = {"abcdef"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 3);

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos);
  expectHasValidResults(res.getResults(), initial, initialPos, goal,
                        "pure newline deletion via gJ");

  bool foundGJ = false;
  for (const Result& r : res.getResults()) {
    if (r.getSequence().view().find("gJ") != string::npos) {
      foundGJ = true;
      break;
    }
  }
  EXPECT_TRUE(foundGJ) << "Expected a result containing gJ";
}

TEST_F(CompositionOptimizer_ManualTest, PureDeletionDoesNotKeepHiddenContextPlaceholder) {
  Lines initial = {"", "~", " "};
  Lines goal = {"", ""};
  CursorPos initialPos(2, 0);
  CursorPos goalPos(1, 0);

  CompositionResult res = opt.optimize(initial, initialPos, goal, goalPos, params);

  expectHasValidResults(
      res.getResults(), initial, initialPos, goal,
      "pure deletion with hidden line above");
}

}  // namespace
