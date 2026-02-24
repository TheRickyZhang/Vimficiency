#pragma once

#include "Effort/RunningEffort.h"
#include "Types/CursorPos.h"
#include "Types/Mode.h"
#include "Types/Sequence.h"
#include "Types/NavContext.h"
#include "Keyboard/KeyedSequence.h"
#include "Types/Lines.h"

// Entire simulated editor state (for now, only position+mode+effort).
class MotionState {
  // Visible, core editor state
  CursorPos pos;
  Mode mode;

  // Progress so far
  Sequence motionSequence;

  // Necessary for ranking states
  double effort;
  double cost;

  // Internal mechanism
  RunningEffort runningEffort;

public:
  MotionState(CursorPos pos, RunningEffort runningEffort, double effort, double cost)
    : pos(pos), runningEffort(runningEffort), effort(effort), cost(cost), mode(Mode::Normal) {
  }

  void reset() {
    pos = CursorPos(0, 0, 0);
    mode = Mode::Normal;
    runningEffort.reset();
  }
  bool operator<(const MotionState& other) const {
    return cost < other.cost;
  }
  bool operator>(const MotionState& other) const {
    return cost > other.cost;
  }

  Pos getKey() const {
    return Pos(pos.line, pos.col);
  }
  CursorPos getPos()                       const { return pos; }
  Mode getMode()                          const { return mode; }
  const Sequence& getSequence()            const { return motionSequence; }
  double getEffort()                      const { return effort; }
  double getCost()                        const { return cost; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // ==========================================================================
  // State transitions - return new state with motion applied
  // ==========================================================================

  // Create new state with motion applied
  // Note: caller must set cost via setCost() after computing heuristic
  [[nodiscard]] MotionState afterMotion(const KeyedSequence& ks, CursorPos endpoint,
                                        const Config& config) const;

  // Overload using pre-computed effort (from EffortBank) — avoids recomputing from keys
  [[nodiscard]] MotionState afterMotion(const KeyedSequence& ks, const RunningEffort& precomputed,
                                        CursorPos endpoint, const Config& config) const;

  // Create new state with counted motion applied (e.g., "3w")
  [[nodiscard]] MotionState afterCountedMotion(const KeyedSequence& baseMotion, int cnt,
                                               CursorPos endpoint, const Config& config,
                                               double extraPenalty) const;

  // Create new state with f-motion applied (e.g., "fx;;")
  [[nodiscard]] MotionState afterFMotion(const KeyedSequence& fMotion, int newCol,
                                         const Config& config) const;

  void setCost(double newCost) { cost = newCost; }

  // For simulated motion without pre-computed endpoint (used by optimizeToRange)
  void applySingleMotionWithEffort(std::string_view motion, const NavContext& navContext,
                                   const Lines& lines, const PhysicalKeys& keys, const Config& config);

  // Keep for parsing arbitrary strings (tests, etc.)
  void applySingleMotion(std::string_view motion, const NavContext& navContext, const Lines& lines);

private:
  void updateEffort(const PhysicalKeys& keys, const Config& config);

  // Internal: apply motion to this state (mutates)
  void applyMotionImpl(const KeyedSequence& ks, CursorPos endpoint, const Config& config);
  void applyMotionImpl(const KeyedSequence& ks, const RunningEffort& precomputed,
                       CursorPos endpoint, const Config& config);
  void applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                              CursorPos endpoint, const Config& config,
                              double extraPenalty);
  void applyFMotionImpl(const KeyedSequence& fMotion, int newCol, const Config& config);
};
