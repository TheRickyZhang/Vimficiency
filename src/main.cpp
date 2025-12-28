#include <bits/stdc++.h>

#include "Optimizer/Config.h"
#include "State/RunningEffort.h"
#include "Optimizer/Optimizer.h"
#include "Editor/Snapshot.h"
#include "State/State.h"
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

  State startingState(start_position, RunningEffort(), 0, 0);
  Config model = Config::uniform();
  Optimizer o(startingState, model);

  vector<Result> res = o.optimizeMovement(
    start_snapshot.lines,
    end_position,
    user_seq
  );

  if(res.empty()) {
    cout << "res is empty" << endl;
  } else {
    cout << "res" << endl;
    for(Result r : res) {
      cout << r.sequence << " " << std::format("{:3f}", r.keyCost) << endl;
    }
  }

  return 0;
}
