#pragma once

#include <bits/stdc++.h>

#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "RunningEffort.h"
#include "Utils/Lines.h"

struct EditStateKey {
  SharedLines lines;  // Shared pointer for efficient comparison
  int line;
  int col;
  Mode mode;
  int startIndex;

  EditStateKey(SharedLines lines, int line, int col, Mode mode, int startIndex) :
    lines(std::move(lines)), line(line), col(col), mode(mode), startIndex(startIndex) {}

  bool operator==(const EditStateKey &other) const {
    // Fast path: same pointer means same content
    if (lines == other.lines) {
      return line == other.line && col == other.col &&
             mode == other.mode && startIndex == other.startIndex;
    }
    // Deep compare only if pointers differ
    return *lines == *other.lines && line == other.line && col == other.col &&
           mode == other.mode && startIndex == other.startIndex;
  }
};

// Hash by pointer for O(1), deep equality handles collisions
struct EditStateKeyHash {
  size_t operator()(const EditStateKey& key) const {
    size_t h = 0;
    // Hash the pointer address - fast O(1)
    combine(h, reinterpret_cast<uintptr_t>(key.lines.get()));
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
// Uses SharedLines for copy-on-write: motions share buffer (O(1)),
// edits trigger copy (O(n)).
class EditState {
  // Visible, core editor state
  SharedLines lines;  // Shared for efficient motion exploration
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
  // Construct from existing SharedLines (shares pointer)
  EditState(SharedLines lines, Position pos, Mode mode,
            RunningEffort runningEffort, int startIndex,
            double effort = 0.0, double cost = 0.0)
      : lines(std::move(lines)), pos(pos), mode(mode),
        runningEffort(runningEffort), startIndex(startIndex),
        effort(effort), cost(cost) {}

  // Construct from Lines (creates new shared pointer)
  EditState(const Lines &linesVal, Position pos, Mode mode,
            RunningEffort runningEffort, int startIndex,
            double effort = 0.0, double cost = 0.0)
      : lines(std::make_shared<const Lines>(linesVal)), pos(pos), mode(mode),
        runningEffort(runningEffort), startIndex(startIndex),
        effort(effort), cost(cost) {}

  bool operator<(const EditState &other) const { return cost < other.cost; }
  bool operator>(const EditState &other) const { return cost > other.cost; }

  EditStateKey getKey() const {
    return EditStateKey(lines, pos.line, pos.col, mode, startIndex);
  }

  // Returns const reference to avoid copying
  const Lines& getLines() const { return *lines; }

  // Returns the shared pointer (for sharing with child states)
  SharedLines getSharedLines() const { return lines; }

  Position getPos() const { return pos; }
  Mode getMode() const { return mode; }
  std::string getMotionSequence() const { return motionSequence; }
  int getStartIndex() const { return startIndex; }
  double getEffort() const { return effort; }
  double getCost() const { return cost; }
  RunningEffort getRunningEffort() const { return runningEffort; }

  void updateCost(double newCost);

  // Motion: modifies pos/mode, shares lines (O(1))
  void applySingleMotion(std::string motion, const KeySequence& keySequence);

  // Edit: copies lines if needed (copy-on-write), then mutates
  // Returns mutable reference to lines for mutation
  Lines& copyLinesForMutation() {
    auto mutableLines = std::make_shared<Lines>(*lines);
    lines = mutableLines;
    return const_cast<Lines&>(*lines);
  }

  void updateEffort(const KeySequence& keySequence, const Config& config);
};
