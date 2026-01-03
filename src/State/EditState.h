#pragma once

#include <bits/stdc++.h>

#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "RunningEffort.h"
#include "Utils/Lines.h"

struct EditStateKey {
  Lines lines;
  int line;
  int col;
  Mode mode;
  int startIndex;
  
  EditStateKey(const Lines& lines, int line, int col, Mode mode, int startIndex) :
    lines(lines), line(line), col(col), mode(mode), startIndex(startIndex) {}

  bool operator==(const EditStateKey &other) const {
    return lines == other.lines && line == other.line && col == other.col &&
           mode == other.mode;
  }
};

// Must depend on all core state of EditState
struct EditStateKeyHash {
  size_t operator()(const EditStateKey& key) const {
    size_t h = 0;
    for (const auto& line : key.lines) {
      combine(h, line);
    }
    combine(h, key.line);
    combine(h, key.col);
    combine(h, static_cast<int>(key.mode));
    combine(h, key.startIndex);
    return h;
  }

private:
  template<typename T>
  static void combine(size_t& h, const T& v) {
    h ^= std::hash<T>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
  }
};

// Entire simulated editor state (for now, only position+mode+effort).
// You can later add: vector<string> lines; registers; etc.
class EditState {
  // Visible, core editor state
  Lines lines;
  Position pos;
  Mode mode;

  // Progress so far
  int startIndex;
  std::string motionSequence;

  // Necessary for ranking states
  double effort;
  double
      cost; // = runningEffort.calculate(motionSequence) + editDistance(state)

  // Internal mechanism
  RunningEffort runningEffort;

public:
  EditState(const Lines &lines, Position pos, Mode mode,
            RunningEffort runningEffort, int startIndex,
            double effort = 0.0, double cost = 0.0)
      : lines(lines), pos(pos), mode(mode),
        runningEffort(runningEffort), startIndex(startIndex),
        effort(effort), cost(cost) {}

  bool operator<(const EditState &other) const { return cost < other.cost; }
  bool operator>(const EditState &other) const { return cost > other.cost; }

  EditStateKey getKey() const {
    return EditStateKey(lines, pos.line, pos.col, mode, startIndex);
  }
  Lines getLines() const { return lines; }
  Position getPos() const { return pos; }
  Mode getMode() const { return mode; }
  std::string getMotionSequence() const { return motionSequence; }
  int getStartIndex() const { return startIndex; }
  double getEffort() const { return effort; }
  double getCost() const { return cost; }
  RunningEffort getRunningEffort() const { return runningEffort; }

  void updateCost(double newCost);
  // In most cases, to update, do in this order:
  // applyMotion
  // updateEffort

  // Must pass context to brute-force compute
  void applySingleMotion(std::string motion, const KeySequence& keySequence);
  
  void updateEffort(const KeySequence& keySequence, const Config& config);
};
