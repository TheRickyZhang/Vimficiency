#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Boundary/NavBoundary.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
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

  auto start_loaded = load_snapshot(start_path);
  auto end_loaded = load_snapshot(end_path);
  if(!start_loaded) {
    cerr << "start snapshot: " << formatSnapshotParseError(start_loaded.error()) << endl;
    return 1;
  }
  if(!end_loaded) {
    cerr << "end snapshot: " << formatSnapshotParseError(end_loaded.error()) << endl;
    return 1;
  }
  Snapshot start_snapshot = std::move(*start_loaded);
  Snapshot end_snapshot = std::move(*end_loaded);

  CursorPos start_position(start_snapshot.row, start_snapshot.col);
  CursorPos end_position(end_snapshot.row, end_snapshot.col);

  debug("starting position:", start_snapshot.row, start_snapshot.col);
  debug("ending position:", end_snapshot.row, end_snapshot.col);

  Config model = Config::uniform();
  NavOptimizer o(model);

  NavContext navContext(
    start_snapshot.windowHeight,
    start_snapshot.scrollAmount
  );

  // CLI uses full file snapshots, so don't exclude G/gg (default NavBoundary)
  NavBoundary boundary;

  // Pass CursorPos and fresh RunningEffort (no prior typing context from CLI)
  vector<LandingResult> res = o.optimize(
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
