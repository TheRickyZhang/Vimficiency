#include <gtest/gtest.h>
#include <ostream>

#include <gtest/gtest.h>
#include <ostream>

#include "Keyboard/CharToKeys.h"
#include "Keyboard/EditToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Optimizer/EditBoundary.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"

using namespace std;

class TempTest : public ::testing::Test {
protected:
  Config config = Config::uniform();

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(10, 1e5, 1.0, 2.0), 3.0);
  }

  EditBoundary noBoundary() {
    return EditBoundary{};
  }

  int countValidResults(const EditResult& res) {
    int count = 0;
    for (int i = 0; i < res.n; i++) {
      for (int j = 0; j < res.m; j++) {
        if (res.adj[i][j].isValid()) count++;
      }
    }
    return count;
  }

  void printEditResult(const EditResult& res, const Lines& beginLines, const Lines& endLines) {
    cout << "EditResult (" << res.n << " x " << res.m << "):" << endl;
    cout << "Begin:\n" << beginLines << "\n";
    cout << "End:\n" << endLines << "\n";

    for (int i = 0; i < res.n; i++) {
      for (int j = 0; j < res.m; j++) {
        const Result& r = res.adj[i][j];
        if (r.isValid()) {
          cout << "["<< i << "][" << j << "] ";
          cout << r << "\n";
        }
      }
    }
  }
};


TEST_F(TempTest, dWNotWorking) {
  Lines begin = {"ab",
  "cd"};  // 5 chars
  Lines end = {"xyz"};      // 3 chars

  // EditState
  EditState start(begin, Position(0, 0), Mode::Normal, RunningEffort(), 0, 0);
  start.applySingleMotion("dd", ALL_EDITS_TO_KEYS.at("dd"), config);
  start.applySingleMotion("cc", ALL_EDITS_TO_KEYS.at("cc"), config);
  start.addTypedSingleChar('x', CHAR_TO_KEYS.at('x'), config);
  start.addTypedSingleChar('y', CHAR_TO_KEYS.at('y'), config);
  start.addTypedSingleChar('z', CHAR_TO_KEYS.at('z'), config);
  EXPECT_EQ(start.getLines(), Lines{"xyz"});
}



