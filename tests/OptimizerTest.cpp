#include <gtest/gtest.h>

#include "Editor/NavContext.h"
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

class OptimizerTest : public ::testing::Test {
protected:
  // This pattern was chosen over alternative of loading everything statically in TestUtils. Not that the naive approach runs into static initialization order problem.
  static vector<string> a1_long_line;
  static vector<string> a2_block_lines;
  static vector<string> a3_spaced_lines;
  static vector<string> m1_main_basic;
  static NavContext navContext;

  static void SetUpTestSuite() {
      a1_long_line = TestFiles::load("a1_long_line.txt");
      a2_block_lines = TestFiles::load("a2_block_lines.txt");
      a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
      m1_main_basic = TestFiles::load("m1_main_basic.txt");

      // Currently the default in my neovim
      navContext = NavContext(0, 39, 39, 19);
  }

  static void TearDownTestSuite() {
    // cout << "-------------------------DEBUG----------------------\n";
    // cout << get_debug_output() << endl;
    // clear_debug_output();
  }


  static State makeState(Position p) { return State(p, RunningEffort(), 0, 0); }

  static vector<Result>
  runOptimizer(const vector<string> &lines, Position start,
               Position end, const string &userSeq,
               const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
               vector<KeyAdjustment> adjustments = {},
               Config config = Config::uniform()
               ) {
    State startState = makeState(start);
    for(KeyAdjustment ka : adjustments) {
      config.keyInfo[static_cast<size_t>(ka.k)].base_cost = ka.cost;
    }

    // Try to explore more, lower search depth for speed
    Optimizer opt(startState, config, 30, 2e4, 1.0, 2.0);
  
    return opt.optimizeMovement(lines, end, navContext, userSeq, allowedMotions);
  }
};

// Static member definitions
vector<string> OptimizerTest::a1_long_line;
vector<string> OptimizerTest::a2_block_lines;
vector<string> OptimizerTest::a3_spaced_lines;
vector<string> OptimizerTest::m1_main_basic;
NavContext OptimizerTest::navContext(0, 0, 0, 0);

TEST_F(OptimizerTest, HorizontalMotions) {
  const string user_seq = "we";
  Position start(0, 0);
  Position end = simulateMotions(start, Mode::Normal, navContext, user_seq, a1_long_line).pos;

  vector<Result> results = runOptimizer(
  a1_long_line,
    start, end, user_seq
  );
  printResults(results);

  EXPECT_TRUE(contains_all(results, {user_seq, "wE", "ee", "EE", "wfal", "wfa;"}))
      << "Missing expected sequences";
}


TEST_F(OptimizerTest, VerticalMotions) {
  const string user_seq = "jjjjj";
  Position start(2, 0);
  Position end = simulateMotions(start, Mode::Normal, navContext, user_seq, a3_spaced_lines).pos;
  // vector<KeyAdjustment> adjustments = {
  //   KeyAdjustment(Key::Key_LBracket, 0.1),
  //   KeyAdjustment(Key::Key_RBracket, 0.1),
  //   KeyAdjustment(Key::Key_9, 0.1),
  //   KeyAdjustment(Key::Key_0, 0.1),
  //   KeyAdjustment(Key::Key_Shift, -0.1),
  // };
  cout << "end: " << end.line << " " << end.col << "\n";

  vector<Result> results = runOptimizer(
  a3_spaced_lines,
    start, end, user_seq,
    // ALL_MOTIONS_TO_KEYS,
    getSlicedMotionToKeys({"j", "k", "G", "{", "}", "(", ")"})
  );

  printResults(results);

  // debugResult(result);
  EXPECT_TRUE(contains_all(results, {"Gk", "G{", "}}", "})}", "}jjj"}))
      << "Missing expected sequences";
}
