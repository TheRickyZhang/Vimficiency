#include <gtest/gtest.h>

#include "TestUtils.h"

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
  static vector<string> m1_main_basic;

  static void SetUpTestSuite() {
      a1_long_line = TestFiles::load("a1_long_line.txt");
      a2_block_lines = TestFiles::load("a2_block_lines.txt");
      m1_main_basic = TestFiles::load("m1_main_basic.txt");
  }


  static State makeState(Position p) { return State(p, RunningEffort(), 0, 0); }

  static vector<string>
  runOptimizer(const vector<string> &lines, Position start,
               Position end, const string &userSeq,
               const Config &config = Config::uniform()) {
    State startState = makeState(start);
    Optimizer opt(startState, config, 20);
    vector<Result> results = opt.optimizeMovement(lines, end, userSeq);
    vector<string> strs;
    strs.reserve(results.size());
    for(auto& r : results) {
      strs.push_back(std::move(r.sequence));
    }
    return strs;
  }
};

// Static member definitions
vector<string> OptimizerMovementTest::a1_long_line;
vector<string> OptimizerMovementTest::a2_block_lines;
vector<string> OptimizerMovementTest::m1_main_basic;

TEST_F(OptimizerMovementTest, SingleLineMovement) {
  string user_seq = "we";
  Position start(0, 0);
  Position end = apply_motions(start, Mode::Normal, user_seq, a1_long_line).pos;

  assert(start != end);

  auto result = runOptimizer(
  a1_long_line,
    start, end, user_seq
  );

  // TODO make this a function or print() with Result
  cout << "Results (" << result.size() << "):" << endl;
  for (const auto& r : result) {
    cout << r << " ";
  }
  cout << endl;

  EXPECT_TRUE(contains_all(result, {user_seq, "ee", "wll", "wwhh"}))
      << "Missing expected sequences";
}
