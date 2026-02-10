#include "MotionOptimizer.h"

#include <limits>
#include <map>
#include <set>

#include "MotionSearchContext.h"
#include "MotionExplorer.h"

#include "Optimizer/BufferIndex.h"
#include "State/PosKey.h"
#include "Utils/Debug.h"

using namespace std;

// Public entry point - dispatches to templated implementation based on direction
MotionResult MotionOptimizer::optimize(
    const Lines& lines,
    const Position& startPos,
    const Position& endPos,
    MotionOptimizerParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const RunningEffort& startingEffort,
    const NavContext& navContext) {

  if (startPos < endPos) {
    return optimizeImpl<true>(lines, startPos, startingEffort, endPos,
                              userSequence, navContext, boundary, params);
  } else {
    return optimizeImpl<false>(lines, startPos, startingEffort, endPos,
                               userSequence, navContext, boundary, params);
  }
}

// Templated implementation - Forward is compile-time constant
template<bool Forward>
MotionResult MotionOptimizer::optimizeImpl(
    const Lines &lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position &endPos,
    string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerParams params) {

  BufferIndex bufferIndex(lines);

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  debug("user effort for sequence", userSequence, "is", userEffort);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);
  MotionExplorer explorer(ctx, endPos, bufferIndex);

  MotionState initialState(startPos, startingEffort, 0.0, 0.0);
  vector<Result> res;

  initialState.setCost(ctx.computePriorityToGoal(initialState, endPos));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    Position pos = s.getPos();

    PosKey stateKey = s.getKey();
    bool isGoal = (stateKey == explorer.getGoalKey());
    bool isSameLine = (pos.line == endPos.line);

    if (isGoal) {
      ctx.markProcessed();
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

    ctx.markProcessed();
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
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerParams);
template MotionResult MotionOptimizer::optimizeImpl<false>(
    const Lines&, const Position&, const RunningEffort&, const Position&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerParams);

// =================================================
// optimizeToRange implementation
// =================================================

// Public entry point - dispatches to templated implementation based on direction
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const Position& startPos,
    const Position& rangeFirst,
    const Position& rangeEnd,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const RunningEffort& startingEffort,
    const NavContext& navContext) {

  // Precondition: startPos must not be in range [rangeFirst, rangeEnd)
  assert(!(startPos >= rangeFirst && startPos < rangeEnd) &&
         "startPos must not be in [rangeFirst, rangeEnd)");

  if (startPos < rangeFirst) {
    return optimizeToRangeImpl<true>(lines, startPos, startingEffort, rangeFirst, rangeEnd,
                                      userSequence, navContext,
                                      boundary, params);
  } else {
    return optimizeToRangeImpl<false>(lines, startPos, startingEffort, rangeFirst, rangeEnd,
                                       userSequence, navContext,
                                       boundary, params);
  }
}

// Templated implementation - Forward is compile-time constant
template<bool Forward>
RangeMotionResult MotionOptimizer::optimizeToRangeImpl(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeEnd,
    string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerRangeParams params) {

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);

  // Use range mode constructor
  MotionExplorer explorer(ctx, rangeFirst, rangeEnd);

  MotionState initialState(startPos, startingEffort, 0.0, 0.0);

  map<PosKey, RangeResult> bestResultByPos;
  vector<RangeResult> allResults;
  int uniquePositionsFound = 0;

  auto isInRange = [&](const Position& pos) {
    return pos >= rangeFirst && pos < rangeEnd;
  };

  // Cap effective maxResults at range size (can't find more positions than exist)
  int rangeSize = lines.spanSize(rangeFirst, rangeEnd);
  int effectiveMaxResults = std::min(params.maxResults, rangeSize);

  initialState.setCost(ctx.computePriorityToRange(initialState, rangeFirst, rangeEnd));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    Position pos = s.getPos();

    PosKey stateKey = s.getKey();
    bool isGoal = isInRange(pos);

    if (isGoal) {
      ctx.markProcessed();
      double effort = s.getRunningEffort().getEffort(config);

      if (params.allowMultiplePerPosition) {
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
          if (uniquePositionsFound >= effectiveMaxResults) {
            debug("optimizeToRange: max unique positions reached (", uniquePositionsFound, "/", rangeSize, ")");
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

    ctx.markProcessed();
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
  int uniqueCount = 0;
  if (params.allowMultiplePerPosition) {
    results = std::move(allResults);
    // Count unique positions in results
    set<PosKey> seen;
    for (const auto& r : results) {
      seen.insert({r.goalPos.line, r.goalPos.col});
    }
    uniqueCount = static_cast<int>(seen.size());
  } else {
    results.reserve(bestResultByPos.size());
    for (auto& [posKey, result] : bestResultByPos) {
      results.push_back(std::move(result));
    }
    uniqueCount = static_cast<int>(results.size());
  }

  SearchStats stats = ctx.getStats(static_cast<int>(results.size()));
  stats.uniquePositionsFound = uniqueCount;

  // Override stop reason for range-specific conditions
  if (uniqueCount >= rangeSize) {
    // Found all positions in range
    stats.stopReason = SearchStopReason::AllResultsFound;
  } else if (static_cast<int>(results.size()) >= params.maxResults) {
    // Hit maxResults limit (only possible with params.allowMultiplePerPosition)
    stats.stopReason = SearchStopReason::MaxResultsFound;
  }

  return RangeMotionResult{std::move(results), stats};
}

// Explicit template instantiations for optimizeToRangeImpl
template RangeMotionResult MotionOptimizer::optimizeToRangeImpl<true>(
    const Lines&, const Position&, const RunningEffort&, const Position&, const Position&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerRangeParams);
template RangeMotionResult MotionOptimizer::optimizeToRangeImpl<false>(
    const Lines&, const Position&, const RunningEffort&, const Position&, const Position&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerRangeParams);

