#include <gtest/gtest.h>

#include "TestUtils.h"

#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/Optimizer.h"
#include "State/RunningEffort.h"
#include "Editor/Snapshot.h"
#include "Editor/Motion.h"
#include "State/State.h"

using namespace std;
// namespace fs = std::filesystem;

class OptimizerMovementTest : public ::testing::Test {
protected:
  // This pattern was chosen over alternative of loading everything statically in TestUtils. Not that the naive approach runs into static initialization order problem.
  static vector<string> a1_long_line;
  static vector<string> a2_block_lines;
  static vector<string> a3_spaced_lines;
  static vector<string> m1_main_basic;

  static void SetUpTestSuite() {
      a1_long_line = TestFiles::load("a1_long_line.txt");
      a2_block_lines = TestFiles::load("a2_block_lines.txt");
      a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
      m1_main_basic = TestFiles::load("m1_main_basic.txt");
  }


  static State makeState(Position p) { return State(p, RunningEffort(), 0, 0); }

  static vector<Result>
  runOptimizer(const vector<string> &lines, Position start,
               Position end, const string &userSeq,
               const MotionToKeys& allowedMotions = ALL_MOTIONS_TO_KEYS,
               vector<KeyAdjustment> adjustments = {},
               Config config = Config::uniform()
               ) {
    State startState = makeState(start);
    for(KeyAdjustment ka : adjustments) {
      config.keyInfo[static_cast<size_t>(ka.k)].base_cost = ka.cost;
    }

    Optimizer opt(startState, config, 30, 1e5, 1.0, 10.0);

  
    return opt.optimizeMovement(lines, end, userSeq, allowedMotions);
  }
};

// Static member definitions
vector<string> OptimizerMovementTest::a1_long_line;
vector<string> OptimizerMovementTest::a2_block_lines;
vector<string> OptimizerMovementTest::a3_spaced_lines;
vector<string> OptimizerMovementTest::m1_main_basic;

TEST_F(OptimizerMovementTest, HorizontalMotions) {
  string user_seq = "we";
  Position start(0, 0);
  Position end = apply_motions(start, Mode::Normal, user_seq, a1_long_line).pos;

  vector<Result> result = runOptimizer(
  a1_long_line,
    start, end, user_seq
  );

  EXPECT_TRUE(contains_all(result, {user_seq, "ee", "wll", "wwhh"}))
      << "Missing expected sequences, got: " << VecStrFmt(result);
}


TEST_F(OptimizerMovementTest, VerticalMotions) {
  string user_seq = "jjjjj";
  Position start(2, 0);
  Position end = apply_motions(start, Mode::Normal, user_seq, a3_spaced_lines).pos;
  // vector<KeyAdjustment> adjustments = {
  //   KeyAdjustment(Key::Key_LBracket, 0.3),
  //   KeyAdjustment(Key::Key_RBracket, 0.3),
  //   KeyAdjustment(Key::Key_9, 0.3),
  //   KeyAdjustment(Key::Key_0, 0.3),
  //   KeyAdjustment(Key::Key_Shift, -0.8),
  // };
  cout << "end: " << end.line << " " << end.col << "\n";

  vector<Result> result = runOptimizer(
  a3_spaced_lines,
    start, end, user_seq
    // ALL_MOTIONS_TO_KEYS
    // getSlicedMotionToKeys({"j", "k", "G", "{", "}", "(", ")"}),
    // adjustments
  );

  // debugResult(result);
  EXPECT_TRUE(contains_all(result, {user_seq, "}}}", "Gk", "G{", "))))"}))
      << "Missing expected sequences, got: " << VecStrFmt(result);
}
