#include "MotionSearchContext.h"

using namespace std;

namespace {

bool isInRange(const CursorPos& pos, const CharRange& range) {
  return pos >= range.begin && pos < range.end;
}

bool isInRange(const CursorPos& pos, const CharLineRange& range) {
  return pos >= range.begin && pos.line < range.endLine;
}

bool isInRange(const CursorPos& pos, const LineCharRange& range) {
  return pos >= CursorPos(range.beginLine, 0) && pos < range.end;
}

template<class RangeT>
void exploreNewStateToTypedRange(MotionSearchContext& ctx,
                                 MotionState&& newState,
                                 const RangeT& range) {
  ctx.motionsEmitted++;

  if (newState.getEffort() > ctx.maxEffort) {
    return;
  }

  double newCost = newState.getCost();
  const Pos newKey = newState.getKey();
  CursorPos pos = newState.getPos();
  auto it = ctx.costMap.find(newKey);
  bool stateInRange = isInRange(pos, range);

  if (it == ctx.costMap.end()) {
    if (!stateInRange) {
      ctx.costMap.emplace(newKey, newCost);
    }
    ctx.pq.push(std::move(newState));
  } else if (newCost <= it->second) {
    it->second = newCost;
    ctx.pq.push(std::move(newState));
  }
}

}  // namespace

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

void MotionSearchContext::exploreNewStateToRange(MotionState&& state, const CharRange& range) {
  exploreNewStateToTypedRange(*this, std::move(state), range);
}

void MotionSearchContext::exploreNewStateToRange(MotionState&& state, const CharLineRange& range) {
  exploreNewStateToTypedRange(*this, std::move(state), range);
}

void MotionSearchContext::exploreNewStateToRange(MotionState&& state, const LineCharRange& range) {
  exploreNewStateToTypedRange(*this, std::move(state), range);
}
