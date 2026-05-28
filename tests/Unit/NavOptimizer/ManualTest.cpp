#include "Unit/NavOptimizer/ManualTestHelpers.h"
#include "Interpreter/MovementInterpreter.h"

using namespace std;

namespace {

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

TEST_F(NavOptimizer_ManualTest, SentenceMotionLandingMatchesMovementReplay) {
  Lines lines = {
      " .b bb d",
      "f abf,c e,b",
      "ee  afda,",
      ",ad.adac",
  };
  CursorPos start(3, 7);
  CursorPos end(0, 2);

  vector<LandingResult> results = runOptimizer(lines, start, end, "");

  ASSERT_FALSE(results.empty());
  for (const auto& result : results) {
    CursorPos replayed = simulateMovements(start, result.getSequence().view(), lines);
    EXPECT_EQ(replayed, result.getGoalPos())
        << "seq='" << result.getSequence() << "'";
  }
}


}  // namespace
