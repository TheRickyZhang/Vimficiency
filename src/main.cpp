#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Boundary/MotionBoundary.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Session/Snapshot.h"
#include "Types/NavContext.h"
#include "Utils/Debug.h"

using namespace std;
namespace fs = std::filesystem;


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

  CursorPos start_position(start_snapshot.row, start_snapshot.col);
  CursorPos end_position(end_snapshot.row, end_snapshot.col);

  debug("starting position:", start_snapshot.row, start_snapshot.col);
  debug("ending position:", end_snapshot.row, end_snapshot.col);

  Config model = Config::uniform();
  MotionOptimizer o(model);

  NavContext navContext(
    start_snapshot.windowHeight,
    start_snapshot.scrollAmount
  );

  // CLI uses full file snapshots, so don't exclude G/gg (default MotionBoundary)
  MotionBoundary boundary;

  // Pass CursorPos and fresh RunningEffort (no prior typing context from CLI)
  vector<Result> res = o.optimize(
    start_snapshot.lines,
    start_position,
    end_position,
    {},
    user_seq,
    boundary
  ).getResults();

  if(res.empty()) {
    cout << "res is empty" << endl;
  } else {
    cout << "res" << endl;
    for(const Result& r : res) {
      cout << r.getSequence() << " " << std::format("{:3f}", r.getCost()) << endl;
    }
  }

  return 0;
}
