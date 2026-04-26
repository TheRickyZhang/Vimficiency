#pragma once

#include "Effort/RunningEffort.h"
#include "Types/CursorPos.h"
#include "Types/Mode.h"
#include "Types/Sequence.h"
#include "Types/NavContext.h"
#include "Keyboard/KeyedSequence.h"
#include "Types/Lines.h"

// Entire simulated editor state (for now, only position+mode+effort).
class NavState {
  // Visible, core editor state
  CursorPos pos;
  Mode mode;

  // Progress so far
  Sequence movementSequence;

  // Necessary for ranking states
  double effort;
  double cost;

  // Internal mechanism
  RunningEffort runningEffort;

public:
  NavState(CursorPos pos, RunningEffort runningEffort, double effort, double cost)
    : pos(pos), runningEffort(runningEffort), effort(effort), cost(cost), mode(Mode::Normal) {
  }

  void reset() {
    pos = CursorPos(0, 0, 0);
    mode = Mode::Normal;
    runningEffort.reset();
  }
  bool operator<(const NavState& other) const {
    return cost < other.cost;
  }
  bool operator>(const NavState& other) const {
    return cost > other.cost;
  }

  Pos getKey() const {
    return Pos(pos.line, pos.col);
  }
  CursorPos getPos()                       const { return pos; }
  Mode getMode()                          const { return mode; }
  const Sequence& getSequence()            const { return movementSequence; }
  double getEffort()                      const { return effort; }
  double getCost()                        const { return cost; }
  const RunningEffort& getRunningEffort() const { return runningEffort; }

  // ==========================================================================
  // State transitions - return new state with motion applied
  // ==========================================================================

  // Create new state with motion applied
  template<class CostFn>
  [[nodiscard]] NavState afterMotion(const KeyedSequence& ks, CursorPos endpoint,
                                        const Config& config, CostFn&& computeCost) const {
    NavState newState = *this;
    newState.applyMotionImpl(ks, endpoint, config);
    newState.cost = computeCost(newState.getPos(), newState.getEffort());
    return newState;
  }

  // Overload using pre-computed effort (from EffortBank) — avoids recomputing from keys
  template<class CostFn>
  [[nodiscard]] NavState afterMotion(const KeyedSequence& ks, const RunningEffort& precomputed,
                                        CursorPos endpoint, const Config& config,
                                        CostFn&& computeCost) const {
    NavState newState = *this;
    newState.applyMotionImpl(ks, precomputed, endpoint, config);
    newState.cost = computeCost(newState.getPos(), newState.getEffort());
    return newState;
  }

  // Create new state with counted motion applied (e.g., "3w")
  template<class CostFn>
  [[nodiscard]] NavState afterCountedMotion(const KeyedSequence& baseMotion, int cnt,
                                               CursorPos endpoint, const Config& config,
                                               double extraPenalty, CostFn&& computeCost) const {
    NavState newState = *this;
    newState.applyCountedMotionImpl(baseMotion, cnt, endpoint, config, extraPenalty);
    newState.cost = computeCost(newState.getPos(), newState.getEffort());
    return newState;
  }

  // Create new state with f-motion applied (e.g., "fx;;")
  template<class CostFn>
  [[nodiscard]] NavState afterFMotion(const KeyedSequence& fMotion, int newCol,
                                         const Config& config, CostFn&& computeCost) const {
    NavState newState = *this;
    newState.applyFMotionImpl(fMotion, newCol, config);
    newState.cost = computeCost(newState.getPos(), newState.getEffort());
    return newState;
  }

  void setCost(double newCost) { cost = newCost; }

  // For simulated motion without pre-computed endpoint (used by optimizeToRange)
  void applySingleMovementWithEffort(std::string_view motion, const NavContext& navContext,
                                   const Lines& lines, const PhysicalKeys& keys, const Config& config);

  // Keep for parsing arbitrary strings (tests, etc.)
  void applySingleMovement(std::string_view motion, const NavContext& navContext, const Lines& lines);

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
