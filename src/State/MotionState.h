#pragma once

#include <bits/stdc++.h>

#include "PosKey.h"
#include "RunningEffort.h"
#include "Sequence.h"
#include "Editor/Position.h"
#include "Editor/Mode.h"
#include "Editor/NavContext.h"
#include "Utils/Lines.h"


// Entire simulated editor state (for now, only position+mode+effort).
class MotionState {
  // Visible, core editor state
  Position pos;
  Mode mode;

  // Progress so far
  Sequence motionSequence;

  // Necessary for ranking states
  double effort;
  double cost;

  // Internal mechanism
  RunningEffort runningEffort;

public:
  MotionState(Position pos, RunningEffort runningEffort, double effort, double cost)
    : pos(pos), runningEffort(runningEffort), effort(effort), cost(cost), mode(Mode::Normal) {
  }

  void reset() {
    pos = Position(0, 0, 0);
    mode = Mode::Normal;
    runningEffort.reset();
  }
  bool operator<(const MotionState& other) const {
    return cost < other.cost;
  }
  bool operator>(const MotionState& other) const {
    return cost > other.cost;
  }

  PosKey getKey() const {
    return PosKey(pos.line, pos.col);
  }
  Position getPos()                       const { return pos; }
  Mode getMode()                          const { return mode; }
  const Sequence& getSequence()            const { return motionSequence; }
  const std::string& getMotionSequence()  const { return motionSequence.keys; }
  double getEffort()                      const { return effort; }
  double getCost()                        const { return cost; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // ==========================================================================
  // State transitions - return new state with motion applied
  // ==========================================================================

  // Create new state with motion applied
  // Note: caller must set cost via setCost() after computing heuristic
  [[nodiscard]] MotionState afterMotion(std::string_view cmd, Position endpoint,
                                        const PhysicalKeys& keys, const Config& config) const;

  // Create new state with counted motion applied (e.g., "3w")
  [[nodiscard]] MotionState afterCountedMotion(std::string_view motion, int cnt,
                                               Position endpoint, const PhysicalKeys& baseKeys,
                                               const Config& config) const;

  // Create new state with f-motion applied (e.g., "fx;;")
  [[nodiscard]] MotionState afterFMotion(std::string_view motion, int newCol,
                                         const PhysicalKeys& keys, const Config& config) const;

  void setCost(double newCost) { cost = newCost; }

  // For simulated motion without pre-computed endpoint (used by optimizeToRange)
  void applySingleMotionWithEffort(std::string_view motion, const NavContext& navContext,
                                   const Lines& lines, const PhysicalKeys& keys, const Config& config);

  // Keep for parsing arbitrary strings (tests, etc.)
  void applySingleMotion(std::string_view motion, const NavContext& navContext, const Lines& lines);

private:
  void updateEffort(const PhysicalKeys& keys, const Config& config);

  // Internal: apply motion to this state (mutates)
  void applyMotionImpl(std::string_view cmd, Position endpoint, const PhysicalKeys& keys, const Config& config);
  void applyCountedMotionImpl(std::string_view motion, int cnt, Position endpoint,
                              const PhysicalKeys& baseKeys, const Config& config);
  void applyFMotionImpl(std::string_view motion, int newCol, const PhysicalKeys& keys, const Config& config);
};
