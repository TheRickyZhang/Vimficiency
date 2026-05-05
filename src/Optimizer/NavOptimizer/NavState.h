#pragma once

#include <utility>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Types/CursorPos.h"
#include "Types/Mode.h"
#include "Types/Sequence.h"
#include "Keyboard/KeyedSequence.h"

// Ranked navigation search state. NavStateFactory is the only construction and
// transition path, so effort and cost are updated together.
class NavState {
  template<class CostFn>
  friend class NavStateFactory;

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

  NavState(CursorPos pos, RunningEffort runningEffort, double effort, double cost)
    : pos(pos), mode(Mode::Normal), effort(effort), cost(cost),
      runningEffort(std::move(runningEffort)) {
  }

public:
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

private:
  // Internal: apply motion to this state (mutates)
  void applyMotionImpl(const KeyedSequence& ks, CursorPos endpoint, const Config& config);
  void applyMotionImpl(const KeyedSequence& ks, const RunningEffort& precomputed,
                       CursorPos endpoint, const Config& config);
  void applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                              CursorPos endpoint, const Config& config,
                              double extraPenalty);
  void applyFMotionImpl(const KeyedSequence& fMotion, int newCol, const Config& config);
};

template<class CostFn>
class NavStateFactory {
  const Config& config;
  CostFn computeCost;

  double costFor(const NavState& state) const {
    return computeCost(state.getPos(), state.getEffort());
  }

public:
  NavStateFactory(const Config& config, CostFn computeCost)
      : config(config), computeCost(std::move(computeCost)) {}

  [[nodiscard]] NavState initial(CursorPos pos) const {
    return initial(pos, RunningEffort());
  }

  [[nodiscard]] NavState initial(CursorPos pos, RunningEffort runningEffort) const {
    double effort = runningEffort.getEffort(config);
    return NavState(pos, std::move(runningEffort), effort, computeCost(pos, effort));
  }

  [[nodiscard]] NavState afterMotion(
      const NavState& state, const KeyedSequence& ks, CursorPos endpoint) const {
    NavState newState = state;
    newState.applyMotionImpl(ks, endpoint, config);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] NavState afterMotion(
      const NavState& state, const KeyedSequence& ks,
      const RunningEffort& precomputed, CursorPos endpoint) const {
    NavState newState = state;
    newState.applyMotionImpl(ks, precomputed, endpoint, config);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] NavState afterCountedMotion(
      const NavState& state, const KeyedSequence& baseMotion, int cnt,
      CursorPos endpoint, double extraPenalty) const {
    NavState newState = state;
    newState.applyCountedMotionImpl(baseMotion, cnt, endpoint, config, extraPenalty);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] NavState afterFMotion(
      const NavState& state, const KeyedSequence& fMotion, int newCol) const {
    NavState newState = state;
    newState.applyFMotionImpl(fMotion, newCol, config);
    newState.cost = costFor(newState);
    return newState;
  }
};

template<class CostFn>
NavStateFactory(const Config&, CostFn) -> NavStateFactory<CostFn>;
