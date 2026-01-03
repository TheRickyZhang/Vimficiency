#pragma once

#include <bits/stdc++.h>

#include "PosKey.h"
#include "RunningEffort.h"
#include "Editor/Position.h"
#include "Editor/Mode.h"
#include "Editor/NavContext.h"


// Entire simulated editor state (for now, only position+mode+effort).
// You can later add: vector<string> lines; registers; etc.
class State {
  // Visible, core editor state
  Position pos;
  Mode mode;

  // Progress so far
  std::string motionSequence;

  // Necessary for ranking states
  double effort;
  double cost;

  // Internal mechanism
  RunningEffort runningEffort;

public:
  State(Position pos, RunningEffort runningEffort, double effort, double cost)
    : pos(pos), runningEffort(runningEffort), effort(effort), cost(cost), mode(Mode::Normal) {
  }

  void reset() {
    pos = Position(0, 0, 0);
    mode = Mode::Normal;
    runningEffort.reset();
  }
  bool operator<(const State& other) const {
    return cost < other.cost;
  }
  bool operator>(const State& other) const {
    return cost > other.cost;
  }

  PosKey getKey() const {
    return PosKey(pos.line, pos.col);
  }
  Position getPos()                const { return pos; }
  Mode getMode()                   const { return mode; }
  std::string getMotionSequence()  const { return motionSequence; }
  double getEffort()               const { return effort; }
  double getCost()                 const { return cost; }
  RunningEffort getRunningEffort() const { return runningEffort; }

  // In most cases, to update, do in this order:
  // applyMotion
  // updateEffort

  // Apply commands change pos, mode, and motionSequence

  // Must pass context to brute-force compute
  void applySingleMotion(std::string motion, const NavContext& navContext, const std::vector<std::string>& lines);

  void applySingleMotionWithKnownColumn(std::string motion, int newCol);

  // When exploring {count}motion, we always know the newPos from index search
  void applyMotionWithKnownPosition(std::string motion, int cnt, const Position& newPos);

  void updateEffort(const KeySequence& keySequence, const Config& config);

  void updateCost(double newCost);

  void setCol(int col) {
    pos.col = col;
  }
};

// Later, you'll change this to dispatch differently depending on s.mode.
// For now, we only support Normal mode motions that don't leave Normal.
// inline void apply_motion(State& s,
//                          string_view motion,
//                          const vector<string>& lines,
//                          const Config& model) {
//   switch(s.mode) {
//     case Mode::Normal:
//     case Mode::Insert:
//     case Mode::Visual:
//   }
// }
