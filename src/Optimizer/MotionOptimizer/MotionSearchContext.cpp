#include "MotionSearchContext.h"

using namespace std;

MotionSearchContext::MotionSearchContext(const Lines& lines,
                                         const NavContext& navContext,
                                         const MotionBoundary& boundary,
                                         const MotionOptimizerParams& params,
                                         const Config& config,
                                         double userEffort,
                                         const CharInterval& range)
    : lines(lines),
      navContext(navContext),
      boundary(boundary),
      params(params),
      config(config),
      bank(config),
      effortWeight(params.effortWeight),
      distanceWeight(params.distanceWeight),
      rangeFirst(range.first),
      rangeLast(range.last),
      maxEffort(userEffort * params.exploreFactor) {
  assert(range.isValid() && "motion search interval must be valid and non-empty");
}

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
