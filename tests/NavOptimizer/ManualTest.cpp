#include "NavOptimizer/ManualTestHelpers.h"

using namespace std;

namespace {

TEST_F(NavOptimizer_ManualTest, HorizontalMotions) {
  const string user_seq = "we";
  CursorPos start(0, 0);
  CursorPos end = simulateMovements(start, user_seq, a1_long_line);

  vector<LandingResult> results = runOptimizer(
  a1_long_line,
    start, end, user_seq
  );

  // Note: "2e" and "ee" are functionally equivalent; optimizer may prefer count-prefixed
  // f motions may not be explored within result limit depending on search order
  EXPECT_TRUE(contains_all(results, {user_seq, "wE"}))
      << "Missing expected sequences";
}

TEST_F(NavOptimizer_ManualTest, ForwardStart_CanUseBackwardCountedVerticalAfterOvershoot) {
  Lines lines = {"a", "b", "c", "d", "e", "f", "g"};
  CursorPos start(0, 0);
  CursorPos end(2, 0);

  vector<KeyAdjustment> adjustments = {
      {Key::Key_J, 12.0},
      {Key::Key_K, 1.0},
      {Key::Key_G, 0.1},
      {Key::Key_Shift, 0.1},
  };

  // Expensive direct "jj", cheap overshoot + return "G4k"
  vector<LandingResult> results = runOptimizer(lines, start, end, "jjjjjjjjjj", adjustments);
  EXPECT_TRUE(contains_all(results, {"G4k"})) << "Expected backward counted vertical after overshoot";
}

TEST_F(NavOptimizer_ManualTest, BackwardStart_CanUseForwardCountedVerticalAfterOvershoot) {
  Lines lines = {"a", "b", "c", "d", "e", "f", "g"};
  CursorPos start(6, 0);
  CursorPos end(4, 0);

  vector<KeyAdjustment> adjustments = {
      {Key::Key_K, 12.0},
      {Key::Key_J, 1.0},
      {Key::Key_G, 0.1},
      {Key::Key_Shift, 0.1},
  };

  // Expensive direct "kk", cheap overshoot + return "gg4j"
  vector<LandingResult> results = runOptimizer(lines, start, end, "kkkkkkkkkk", adjustments);
  EXPECT_TRUE(contains_all(results, {"gg4j"})) << "Expected forward counted vertical after overshoot";
}


// =============================================================================
// optimize tests
// =============================================================================

TEST_F(NavOptimizer_ManualTest, RangeBasic_SameLine) {
  // Target range is columns 5-10 on line 0
  Lines lines = {"hello world this is a test line"};
  CursorPos start(0, 0);
  CursorPos rangeBegin(0, 5);
  CursorPos rangeEnd(0, 10);

  vector<LandingResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "lllll");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
  for (const auto& r : results) {
    EXPECT_GE(r.getGoalPos().col, 5) << "End position should be in range";
    EXPECT_LE(r.getGoalPos().col, 10) << "End position should be in range";
  }
}

TEST_F(NavOptimizer_ManualTest, RangeBasic_MultiLine) {
  // Target range spans multiple lines
  Lines lines = {"line one", "line two", "line three", "line four"};
  CursorPos start(0, 0);
  CursorPos rangeBegin(1, 0);
  CursorPos rangeEnd(2, 5);

  vector<LandingResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
  for (const auto& r : results) {
    CursorPos p = r.getGoalPos();
    bool inRange = (p >= rangeBegin && p <= rangeEnd);
    EXPECT_TRUE(inRange) << "End position (" << p.line << ", " << p.col << ") should be in range";
  }
}

TEST_F(NavOptimizer_ManualTest, RangeFromMiddle) {
  // Start from middle of file, target range at end
  Lines lines = {"aaa", "bbb", "ccc", "ddd", "eee"};
  CursorPos start(2, 1);
  CursorPos rangeBegin(4, 0);
  CursorPos rangeEnd(4, 2);

  vector<LandingResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
}

TEST_F(NavOptimizer_ManualTest, RangeWithWordMotions) {
  // Test that word motions can land in range
  Lines lines = {"one two three four five six"};
  CursorPos start(0, 0);
  CursorPos rangeBegin(0, 8);   // "three" starts at 8
  CursorPos rangeEnd(0, 17);    // "four" ends at 17

  vector<LandingResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "www");

  EXPECT_FALSE(results.empty()) << "Should find paths using word motions";
}

// =============================================================================
// NavBoundary tests
// =============================================================================

class NavBoundaryTest : public ::testing::Test {
protected:
  struct CountedCandidate {
    string sequence;
    CursorPos endpoint;
  };

  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  // Helper to run optimizer with specific boundary
  static vector<LandingResult>
  runWithBoundary(const Lines& lines, CursorPos start, CursorPos end,
                  const string& userSeq, const NavBoundary& boundary,
                  Config config = Config::uniform()) {
    NavOptimizer opt(config);
    return opt.optimize(lines, start, end,
                        NavOptimizerParams{}
                            .withMaxResults(30)
                            .withMaxNodesPopped(20000)
                            .withMaxResultsPerEndPos(2),
                        userSeq, boundary, navContext).getResults();
  }

  // Helper to check if results contain a sequence
  static bool hasSequence(const vector<LandingResult>& results, const string& seq) {
    return std::any_of(results.begin(), results.end(),
        [&seq](const Result& r) { return r.getSequence() == seq; });
  }

  static vector<CountedCandidate> collectCountedCandidates(
      const Lines& lines,
      CursorPos start,
      CursorPos goal,
      const NavBoundary& boundary,
      NavOptimizerParams params = NavOptimizerParams{}
          .withMinCountRepeat(2)
          .withMaxCountRepeat(8)) {
    Config config = Config::uniform();
    BufferIndex index(lines);
    CharInterval goalRange(goal, goal);
    NavExplorer explorer(lines, navContext, boundary, params, goalRange, index, 0);

    auto score = [](CursorPos, double effort) { return effort; };
    NavStateFactory states(config, score);
    NavState base = states.initial(start);

    vector<CountedCandidate> candidates;
    auto onCounted = [&](KSId, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double) {
      KeyedSequence counted(count, ks);
      candidates.push_back({counted.seq.str(), endpoint});
    };
    auto onFMotion = [](const KeyedSequence&, int) {};
    explorer.exploreCountedMotions(base, onCounted, onFMotion);
    return candidates;
  }

  static bool hasCountedCandidate(
      const vector<CountedCandidate>& candidates,
      const string& sequence,
      CursorPos endpoint) {
    return std::any_of(candidates.begin(), candidates.end(),
        [&](const CountedCandidate& candidate) {
          return candidate.sequence == sequence && candidate.endpoint == endpoint;
        });
  }
};

NavContext NavBoundaryTest::navContext(0, 0);

}  // namespace
