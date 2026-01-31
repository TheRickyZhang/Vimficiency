#include "MotionOptimizer.h"
#include "MotionSearchContext.h"
#include "MotionExplorer.h"

#include <limits>

#include "BufferIndex.h"
#include "State/PosKey.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"

using namespace std;

// Public entry point - dispatches to templated implementation based on direction
MotionResult MotionOptimizer::optimize(
    const Lines &lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position &endPos,
    const string &userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys &rawMotionToKeys,
    OptimizerParams params) {

  if (startPos < endPos) {
    return optimizeImpl<true>(lines, startPos, startingEffort, endPos,
                              userSequence, navContext, boundary, rawMotionToKeys, params);
  } else {
    return optimizeImpl<false>(lines, startPos, startingEffort, endPos,
                               userSequence, navContext, boundary, rawMotionToKeys, params);
  }
}

// Templated implementation - Forward is compile-time constant
template<bool Forward>
MotionResult MotionOptimizer::optimizeImpl(
    const Lines &lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position &endPos,
    const string &userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys &rawMotionToKeys,
    OptimizerParams params) {

  BufferIndex bufferIndex(lines);

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  debug("user effort for sequence", userSequence, "is", userEffort);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);
  MotionExplorer explorer(ctx, endPos, bufferIndex, rawMotionToKeys);

  MotionState initialState(startPos, startingEffort, 0.0, 0.0);
  vector<Result> res;

  initialState.updateCost(ctx.computePriorityToGoal(initialState, endPos));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    Position pos = s.getPos();

    PosKey stateKey = s.getKey();
    bool isGoal = (stateKey == explorer.getGoalKey());
    bool isSameLine = (pos.line == endPos.line);

    if (isGoal) {
      res.emplace_back(s.getMotionSequence(), s.getRunningEffort().getEffort(config));
      if (res.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    } else {
      if (ctx.isStale(s)) {
        ctx.statesSkipped++;
        continue;
      }
    }

    debug("\"" + s.getMotionSequence() + "\"", s.getCost());

    // Track this state if debugging
    ctx.trackState(s);

    // Explore standard motions - use directional pruning if enabled
    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotions(s);
    } else {
      explorer.exploreAllStandardMotions(s);
    }

    // Explore directional motions (f/F, count-based word/paragraph/sentence)
    // Forward is compile-time constant - no runtime branch
    explorer.exploreDirectionalMotions<Forward>(s, isSameLine);
  }

  debug("---costMap---");
  for (auto [state, cost] : ctx.costMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  SearchStats stats = ctx.getStats(static_cast<int>(res.size()));
  return {.results = std::move(res), .stats = stats};
}

// Explicit template instantiations
template MotionResult MotionOptimizer::optimizeImpl<true>(
    const Lines&, const Position&, const RunningEffort&, const Position&,
    const string&, const NavContext&, const MotionBoundary&, const MotionToKeys&, OptimizerParams);
template MotionResult MotionOptimizer::optimizeImpl<false>(
    const Lines&, const Position&, const RunningEffort&, const Position&,
    const string&, const NavContext&, const MotionBoundary&, const MotionToKeys&, OptimizerParams);

// =================================================
// optimizeToRange implementation
// =================================================

// Public entry point - dispatches to templated implementation based on direction
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeLast,
    const string& userSequence,
    NavContext& navContext,
    bool allowMultiplePerPosition,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    OptimizerParams params) {

  // Precondition: startPos must not be in range
  assert(!(startPos >= rangeFirst && startPos <= rangeLast) &&
         "startPos must not be in [rangeFirst, rangeLast]");

  if (startPos < rangeFirst) {
    return optimizeToRangeImpl<true>(lines, startPos, startingEffort, rangeFirst, rangeLast,
                                      userSequence, navContext, allowMultiplePerPosition,
                                      boundary, rawMotionToKeys, params);
  } else {
    return optimizeToRangeImpl<false>(lines, startPos, startingEffort, rangeFirst, rangeLast,
                                       userSequence, navContext, allowMultiplePerPosition,
                                       boundary, rawMotionToKeys, params);
  }
}

// Templated implementation - Forward is compile-time constant
template<bool Forward>
RangeMotionResult MotionOptimizer::optimizeToRangeImpl(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeLast,
    const string& userSequence,
    NavContext& navContext,
    bool allowMultiplePerPosition,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    OptimizerParams params) {

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);

  // Use range mode constructor
  MotionExplorer explorer(ctx, rangeFirst, rangeLast);

  MotionState initialState(startPos, startingEffort, 0.0, 0.0);

  map<PosKey, RangeResult> bestResultByPos;
  vector<RangeResult> allResults;
  int uniquePositionsFound = 0;

  auto isInRange = [&](const Position& pos) {
    return pos >= rangeFirst && pos <= rangeLast;
  };

  initialState.updateCost(ctx.computePriorityToRange(initialState, rangeFirst, rangeLast));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    Position pos = s.getPos();

    PosKey stateKey = s.getKey();
    bool isGoal = isInRange(pos);

    if (isGoal) {
      double effort = s.getRunningEffort().getEffort(config);

      if (allowMultiplePerPosition) {
        allResults.emplace_back(s.getMotionSequence(), effort, pos);
        if (allResults.size() >= static_cast<size_t>(params.maxResults)) {
          debug("optimizeToRange: max results reached");
          break;
        }
      } else {
        auto it = bestResultByPos.find(stateKey);
        if (it == bestResultByPos.end()) {
          bestResultByPos.emplace(stateKey, RangeResult(s.getMotionSequence(), effort, pos));
          uniquePositionsFound++;
          if (uniquePositionsFound >= params.maxResults) {
            debug("optimizeToRange: max unique positions reached");
            break;
          }
        } else if (effort < it->second.keyCost) {
          it->second = RangeResult(s.getMotionSequence(), effort, pos);
        }
      }
      continue;
    } else {
      if (ctx.isStale(s)) continue;
    }

    debug("\"" + s.getMotionSequence() + "\"", s.getCost());

    // Track this state if debugging
    ctx.trackState(s);

    // Explore standard motions - use directional pruning if enabled
    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotionsToRange(s);
    } else {
      explorer.exploreAllStandardMotions(s);
    }
  }

  debug("---costMap---");
  map<PosKey, double> tempMap(ctx.costMap.begin(), ctx.costMap.end());
  for (auto [state, cost] : tempMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  vector<RangeResult> results;
  if (allowMultiplePerPosition) {
    results = std::move(allResults);
  } else {
    results.reserve(bestResultByPos.size());
    for (auto& [posKey, result] : bestResultByPos) {
      results.push_back(std::move(result));
    }
  }

  SearchStats stats = ctx.getStats(static_cast<int>(results.size()));
  return {.results = std::move(results), .stats = stats};
}

// Explicit template instantiations for optimizeToRangeImpl
template RangeMotionResult MotionOptimizer::optimizeToRangeImpl<true>(
    const Lines&, const Position&, const RunningEffort&, const Position&, const Position&,
    const string&, NavContext&, bool, const MotionBoundary&, const MotionToKeys&, OptimizerParams);
template RangeMotionResult MotionOptimizer::optimizeToRangeImpl<false>(
    const Lines&, const Position&, const RunningEffort&, const Position&, const Position&,
    const string&, NavContext&, bool, const MotionBoundary&, const MotionToKeys&, OptimizerParams);

// Overload without userSequence - uses unbounded effort exploration
MotionResult MotionOptimizer::optimize(
    const Lines& lines,
    const Position& startPos,
    const Position& endPos,
    const NavContext& navigationContext,
    const MotionBoundary& boundary,
    OptimizerParams params) {
  return optimize(
      lines,
      startPos,
      RunningEffort(),
      endPos,
      "",
      navigationContext,
      boundary,
      EXPLORABLE_MOTIONS,
      params
  );
}
