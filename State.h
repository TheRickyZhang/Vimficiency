#include <bits/stdc++.h>
using namespace std;

#include "EffortState.h"

enum class Mode : uint8_t {
  Normal,
  Insert,
  Visual,
};

using PosKey = pair<int, int>;
struct Position {
  int line = 0;
  int col  = 0;
  int targetCol = 0;
  void setCol(int c) {
    col = targetCol = c;
  }
};

// Entire simulated editor state (for now, only position+mode+effort).
// You can later add: vector<string> lines; registers; etc.
struct State {
  Position pos;
  Mode mode;

  int cost;
  EffortState effort;

  PosKey key;

  State(Position pos, int cost, EffortState effort)
    : pos(pos), mode(Mode::Normal), cost(cost), effort(effort) {
    key = {pos.line, pos.targetCol};
  }

  void reset() {
    pos = Position{};
    mode = Mode::Normal;
    effort.reset();
  }
  bool operator<(const State& other) {
    return cost < other.cost;
  }
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------



// -----------------------------------------------------------------------------
// Normal-mode motion semantics (MVP subset)
// -----------------------------------------------------------------------------

// Does not change mode or modify text
// Returns a 
inline void apply_normal_motion(State& s,
                                string_view motion,
                                const vector<string>& lines
                                ) {
  Position& p = s.pos;
  const int n = (int)lines.size();

  auto clampCol = [&](int col, int lineIdx) {
    assert(lineIdx >= 0 && lineIdx < n);
    int len = lines[lineIdx].size();
    return len == 0 ? 0 : clamp(col, 0, len - 1);
  };

  auto moveCol = [&](int x) {
      p.setCol(clampCol(p.col + x, s.pos.line));
  };

  auto moveLine = [&](int x) {
    p.line = clamp(x, 0, n-1);
    p.col = clampCol(p.col, p.line);
  };

  if(motion == "h") {
    moveCol(-1);
  }
  else if(motion == "l") {
    moveCol(1);
  }
  else if(motion == "j") {
    moveLine(1);
  }
  else if(motion == "k") {
    moveLine(-1);
  }
  else if(motion == "0") {
    p.setCol(0);
  }
  else if(motion == "$") {
    int len = lines[p.line].size();
    p.setCol(len == 0 ? 0 : len - 1);
  }
  else if(motion == "^") {
    int len = lines[p.line].size();
    int col = 0;
    const string& line = lines[p.line];
    while(col < len && isspace((unsigned char)line[col])) ++col;
    p.setCol(col);
  }
  
  else if(motion == "gg") {
    p.line = 0;
    p.col = clampCol(p.col, p.line);
  }
  else if(motion == "G") {
    p.line = n - 1;
    p.col = clampCol(p.col, p.line);
  }
  else {
    cout<<"err"<<endl;
  }
}

// -----------------------------------------------------------------------------
// High-level entry: apply "one motion" to VimState, respecting mode
// -----------------------------------------------------------------------------

// Later, you'll change this to dispatch differently depending on s.mode.
// For now, we only support Normal mode motions that don't leave Normal.
// inline void apply_motion(State& s,
//                          string_view motion,
//                          const vector<string>& lines,
//                          const Config& model) {
//   switch(s.mode) {
//     case Mode::Normal:
//       apply_normal_motion(s, motion, lines);
//       break;
//
//     case Mode::Insert:
//       cout<<"not done bozo"<<endl;
//       break;
//
//     case Mode::Visual:
//       cout<<"not done bozo"<<endl;
//       break;
//   }
// }
