#include <bits/stdc++.h>

#include "Optimizer/Config.h"
#include "Optimizer/ImpliedExclusions.h"
#include "State/RunningEffort.h"
#include "Optimizer/MovementOptimizer.h"
#include "Editor/Snapshot.h"
#include "Editor/NavContext.h"
#include "State/MotionState.h"
#include "Utils/Debug.h"

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
  if(argc != 4) {
    cerr << "must pass in file paths for start path, end path, user sequence";
    return 1;
  }

  fs::path start_path = argv[1];
  fs::path end_path = argv[2];
  string user_seq = argv[3];

  Snapshot start_snapshot = load_snapshot(start_path); 
  Snapshot end_snapshot = load_snapshot(end_path); 

  Position start_position(start_snapshot.row, start_snapshot.col);
  Position end_position(end_snapshot.row, end_snapshot.col);

  debug("starting position:", start_snapshot.row, start_snapshot.col);
  debug("ending position:", end_snapshot.row, end_snapshot.col);

  Config model = Config::uniform();
  MovementOptimizer o(model);

  NavContext navContext(
    start_snapshot.windowHeight,
    start_snapshot.scrollAmount
  );

  // CLI uses full file snapshots, so don't exclude G/gg
  ImpliedExclusions impliedExclusions(false, false);

  // Pass Position and fresh RunningEffort (no prior typing context from CLI)
  vector<Result> res = o.optimize(
    start_snapshot.lines,
    start_position,
    RunningEffort(),
    end_position,
    user_seq,
    navContext,
    SearchParams(),  // default search parameters
    impliedExclusions
  );

  if(res.empty()) {
    cout << "res is empty" << endl;
  } else {
    cout << "res" << endl;
    for(Result r : res) {
      cout << r.getSequenceString() << " " << std::format("{:3f}", r.keyCost) << endl;
    }
  }

  return 0;
}
