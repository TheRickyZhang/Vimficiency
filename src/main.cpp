#include <bits/stdc++.h>

#include "Config.h"
#include "EffortState.h"
#include "TemplateKeyboard.h"
#include "State.h"
#include "Optimizer.h"
#include "Testing/TestUtils.h"

using namespace std;
namespace fs = std::filesystem;

int main() {
  Config model;
  fill_equal(model);

  State startingState(Position(0, 0), EffortState(), 0, 0);
  Optimizer o(startingState, model, 1);

  fs::path fn = fs::path(TESTFILES_DIR) / "uniform.txt";
  ifstream fin(fn);
  if(!fin) {
      throw runtime_error("Can't read uniform.txt");
  }

  vector<string> lines = readLines(fin);
  vector<Result> res = o.optimizeMovement(lines, 
    Position(1, 1), "jjkl"
  );

  if(res.empty()) {
    cout << "res is empty" << endl;
  } else {
    for(Result r : res) {
      cout << r.sequence << " " << std::format("{:3f}", r.keyCost) << endl;
    }
  }

  return 0;
}
