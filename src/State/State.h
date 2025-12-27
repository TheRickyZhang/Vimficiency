#pragma once

#include <bits/stdc++.h>

#include "State/RunningEffort.h"

enum class Mode : uint8_t {
  Normal,
  Insert,
  Visual,
};

using PosKey = std::pair<int, int>;
struct Position {
  int line = 0;
  int col  = 0;
  int targetCol = 0;
  Position(int l, int c) : line(l), col(c), targetCol(c) {}
  Position(int l, int c, int tc) : line(l), col(c), targetCol(tc) {}

  void setCol(int c) {
    col = targetCol = c;
  }
};

// Entire simulated editor state (for now, only position+mode+effort).
// You can later add: vector<string> lines; registers; etc.
struct State {
  Position pos;
  Mode mode;
  RunningEffort effortState;

  double effort;
  double cost;
  std::string sequence;


  State(Position pos, RunningEffort effortState, int effort, int cost)
    : pos(pos), effortState(effortState), effort(effort), cost(cost), mode(Mode::Normal) {
  }

  void reset() {
    pos = Position(0, 0, 0);
    mode = Mode::Normal;
    effortState.reset();
  }
  bool operator<(const State& other) const {
    return cost < other.cost;
  }
  bool operator>(const State& other) const {
    return cost > other.cost;
  }
  PosKey getKey() const {
    return std::make_pair(pos.line, pos.col);
  }

  void apply_normal_motion(std::string motion, const std::vector<std::string>& lines);
};


// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------



// -----------------------------------------------------------------------------
// Normal-mode motion semantics (MVP subset)
// -----------------------------------------------------------------------------

// Does not change mode or modify text
// Returns a 

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
//       debug("not done bozo");
//       break;
//
//     case Mode::Visual:
//       debug("not done bozo");
//       break;
//   }
// }
