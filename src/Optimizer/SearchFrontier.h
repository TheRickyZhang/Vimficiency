#pragma once

#include <utility>

namespace Search {

template<class State, class PriorityQueue, class CostMap, class InGoalFn>
inline bool enqueueRangeState(State&& state,
                              PriorityQueue& pq,
                              CostMap& costMap,
                              double maxEffort,
                              InGoalFn&& isGoalState) {
  if (state.getEffort() > maxEffort) return false;

  const double newCost = state.getCost();
  const auto key = state.getKey();
  auto it = costMap.find(key);
  const bool stateInGoal = isGoalState(state.getPos());

  if (it == costMap.end()) {
    if (!stateInGoal) {
      costMap.emplace(key, newCost);
    }
    pq.push(std::move(state));
    return true;
  }

  if (newCost <= it->second) {
    it->second = newCost;
    pq.push(std::move(state));
    return true;
  }

  return false;
}

template<class State, class PriorityQueue, class CostMap, class PendingByStart,
         class StartActive>
inline bool enqueueImprovedTransformState(State&& state,
                                     PriorityQueue& pq,
                                     CostMap& costMap,
                                     PendingByStart& pendingByStart,
                                     const StartActive& startActive) {
  const int startIndex = state.getStartIndex();
  if (!startActive[startIndex]) return false;

  const auto key = state.getKey();
  const double newCost = state.getCost();
  auto it = costMap.find(key);
  if (it != costMap.end() && newCost > it->second) return false;

  costMap[key] = newCost;
  pendingByStart[startIndex]++;
  pq.push(std::move(state));
  return true;
}

}  // namespace Search
