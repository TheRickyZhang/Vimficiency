#pragma once

#include <bits/stdc++.h>

#include "RunningEffort.h"
#include "Editor/Position.h"
#include "Editor/Mode.h"


using PosKey = std::pair<int, int>;

// Entire simulated editor state (for now, only position+mode+effort).
// You can later add: vector<string> lines; registers; etc.
struct State {
  // Visible, core editor state
  Position pos;
  Mode mode;

  // Progress so far
  std::string motionSequence;

  // Necessary for ranking states
  double effort;
  double cost;

  // Internal mechanism
  RunningEffort effortState;

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

  void set_col(int col) {
    debug("setting to new col, be careful", col);
    pos.col = col;
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
