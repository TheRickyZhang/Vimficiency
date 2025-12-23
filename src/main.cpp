#include <bits/stdc++.h>

#include "Config.h"
#include "EffortState.h"
#include "Optimizer.h"
#include "Snapshot.h"
#include "State.h"
#include "TemplateKeyboard.h"
#include "Testing/TestUtils.h"

using namespace std;
namespace fs = std::filesystem;

// TODO move to testing file (oh and write proper tests!)
  // fs::path fn = fs::path(TESTFILES_DIR) / "uniform.txt";
  // ifstream fin(fn);
  // if(!fin) {
  //     throw runtime_error("Can't read uniform.txt");
  // }
  // vector<string> lines = readLines(fin);

int main(int argc, char* argv[]) {
  if(argc < 3) {
    cout << "must pass in file paths for start / end states for now";
    return 1;
  }

  fs::path start_path = argv[1];
  fs::path end_path = argv[2];

  Snapshot start_snapshot = load_snapshot(start_path); 
  Snapshot end_snapshot = load_snapshot(end_path); 

  Position start_position(start_snapshot.row, start_snapshot.col);
  Position end_position(end_snapshot.row, end_snapshot.col);

  State startingState(start_position, EffortState(), 0, 0);
  Config model;
  fill_equal(model);
  Optimizer o(startingState, model, 1);

  vector<Result> res = o.optimizeMovement(start_snapshot.lines, end_position, "jjkl"
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
