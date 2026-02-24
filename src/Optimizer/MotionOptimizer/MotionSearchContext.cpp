#include "MotionSearchContext.h"

using namespace std;

MotionSearchContext::MotionSearchContext(const Lines& lines,
                                         const NavContext& navContext,
                                         const MotionBoundary& boundary,
                                         const MotionOptimizerParams& params,
                                         const Config& config,
                                         double userEffort)
    : lines(lines),
      navContext(navContext),
      boundary(boundary),
      params(params),
      config(config),
      bank(config),
      effortWeight(params.effortWeight),
      distanceWeight(params.distanceWeight),
      maxEffort(userEffort * params.exploreFactor) {}

void MotionSearchContext::exploreNewState(MotionState&& newState, const Pos& goalKey) {
  motionsEmitted++;

  // Prune if effort exceeds threshold
  if (newState.getEffort() > maxEffort) {
    return;
  }

  double newCost = newState.getCost();
  const Pos newKey = newState.getKey();
  auto it = costMap.find(newKey);

  if (it == costMap.end()) {
    // New state - don't cache goal positions (allow multiple results)
    if (newKey != goalKey) {
      costMap.emplace(newKey, newCost);
    }
    pq.push(std::move(newState));
  }
  // Allow equal costs for more exploration - finds all optimal paths
  else if (newCost <= it->second) {
    it->second = newCost;
    pq.push(std::move(newState));
  }
}

void MotionSearchContext::exploreNewStateToRange(MotionState&& newState,
                                                  Pos rangeBegin,
                                                  Pos rangeEnd) {
  motionsEmitted++;

  // Prune if effort exceeds threshold
  if (newState.getEffort() > maxEffort) {
    return;
  }

  double newCost = newState.getCost();
  const Pos newKey = newState.getKey();
  CursorPos pos = newState.getPos();
  auto it = costMap.find(newKey);

  // Check if position is in goal range [rangeBegin, rangeEnd)
  bool isInRange = pos >= rangeBegin && pos < rangeEnd;

  if (it == costMap.end()) {
    // New state - don't cache positions in range (allow multiple results)
    if (!isInRange) {
      costMap.emplace(newKey, newCost);
    }
    pq.push(std::move(newState));
  }
  // Allow equal costs for more exploration - finds all optimal paths
  else if (newCost <= it->second) {
    it->second = newCost;
    pq.push(std::move(newState));
  }
}
