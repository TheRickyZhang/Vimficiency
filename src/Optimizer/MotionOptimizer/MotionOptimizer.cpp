#include "MotionOptimizer.h"

#include <limits>
#include <map>
#include <set>

#include "MotionSearchContext.h"
#include "MotionExplorer.h"

#include "BufferIndex.h"
#include "Types/Pos.h"
#include "Utils/Debug.h"

using namespace std;

namespace {

int targetSize(const Lines& lines, const InclusiveCharRange& targetRange) {
  CursorPos targetFirst(targetRange.firstPos.line, targetRange.firstPos.col);
  CursorPos targetLast(targetRange.lastPos.line, targetRange.lastPos.col);
  return lines.spanSize(targetFirst, lines.getNextPosUnbounded(targetLast));
}

}  // namespace

// Public entry point - dispatches to templated implementation based on direction
MotionResult MotionOptimizer::optimize(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
    MotionOptimizerParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  params.assertValidCountRepeatBounds();
  if (initialPos < goalPos) {
    return optimizeImpl<true>(lines, initialPos, goalPos,
                              userSequence, navContext, boundary, params);
  } else {
    return optimizeImpl<false>(lines, initialPos, goalPos,
                               userSequence, navContext, boundary, params);
  }
}

// Templated implementation - Forward is compile-time constant
template<bool Forward>
MotionResult MotionOptimizer::optimizeImpl(
    const Lines &lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
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

  InclusiveCharRange targetRange(goalPos, goalPos);
  MotionExplorer explorer(ctx, targetRange, bufferIndex, 0);

  Pos goalKey(goalPos.line, goalPos.col);

  MotionState initialState(initialPos, RunningEffort(), 0.0, 0.0);
  vector<Result> res;

  initialState.setCost(ctx.computePriorityToGoal(initialState, goalPos));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    CursorPos pos = s.getPos();

    Pos stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);
    bool isSameLine = (pos.line == goalPos.line);

    if (isGoal) {
      ctx.markProcessed();
      res.emplace_back(s.getSequence().str(), s.getRunningEffort().getEffort(config));
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
    debug("\"" + s.getSequence().str() + "\"", s.getCost());

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

  MotionSearchStats stats = ctx.getStats(static_cast<int>(res.size()));
  return MotionResult(std::move(res), stats);
}

// Explicit template instantiations
template MotionResult MotionOptimizer::optimizeImpl<true>(
    const Lines&, const CursorPos&, const CursorPos&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerParams);
template MotionResult MotionOptimizer::optimizeImpl<false>(
    const Lines&, const CursorPos&, const CursorPos&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerParams);

// =================================================
// optimizeToRange implementation
// =================================================

// Convenience overload that builds a local BufferIndex.
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const InclusiveCharRange& targetRange,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  BufferIndex localIndex(lines);
  return optimizeToRange(lines, startPos, targetRange, params, userSequence,
                         boundary, navContext, localIndex, 0);
}

// Overloads with caller-provided BufferIndex.
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const InclusiveCharRange& targetRange,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  assert(!targetRange.contains(startPos) && "startPos must not be in target range");
  if (startPos < targetRange.firstPos) {
    return optimizeToRangeImpl<true>(lines, startPos, targetRange, userSequence,
                                     navContext, boundary, params,
                                     bufferIndex, lineOffset);
  }
  return optimizeToRangeImpl<false>(lines, startPos, targetRange, userSequence,
                                    navContext, boundary, params,
                                    bufferIndex, lineOffset);
}

// Templated implementation - Forward is compile-time constant
// Precondition: bufferIndex/lineOffset are always valid (resolved by public optimizeToRange)
template<bool Forward>
RangeMotionResult MotionOptimizer::optimizeToRangeImpl(
    const Lines& lines,
    const CursorPos& startPos,
    const InclusiveCharRange& targetRange,
    string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerRangeParams params,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  params.assertValidCountRepeatBounds();

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);
  MotionExplorer explorer(ctx, targetRange, bufferIndex, lineOffset);

  MotionState initialState(startPos, RunningEffort(), 0.0, 0.0);

  map<Pos, RangeResult> bestResultByPos;
  vector<RangeResult> allResults;
  int uniquePositionsFound = 0;

  // Cap effective maxResults at range size (can't find more positions than exist)
  int rangeSize = targetSize(lines, targetRange);
  int effectiveMaxResults = std::min(params.maxResults, rangeSize);

  initialState.setCost(ctx.computePriorityToRange(initialState, targetRange));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    CursorPos pos = s.getPos();

    Pos stateKey = s.getKey();
    bool isGoal = targetRange.contains(stateKey);

    if (isGoal) {
      ctx.markProcessed();
      double effort = s.getRunningEffort().getEffort(config);

      if (params.allowMultiplePerPosition) {
        allResults.emplace_back(s.getSequence().str(), effort, pos);
        if (allResults.size() >= static_cast<size_t>(params.maxResults)) {
          debug("optimizeToRange: max results reached");
          break;
        }
      } else {
        auto it = bestResultByPos.find(stateKey);
        if (it == bestResultByPos.end()) {
          bestResultByPos.emplace(stateKey, RangeResult(s.getSequence().str(), effort, pos));
          uniquePositionsFound++;
          if (uniquePositionsFound >= effectiveMaxResults) {
            debug("optimizeToRange: max unique positions reached (", uniquePositionsFound, "/", rangeSize, ")");
            break;
          }
        } else if (effort < it->second.getCost()) {
          it->second = RangeResult(s.getSequence().str(), effort, pos);
        }
      }
      continue;
    } else {
      if (ctx.isStale(s)) continue;
    }

    ctx.markProcessed();
    debug("\"" + s.getSequence().str() + "\"", s.getCost());

    // Track this state if debugging
    ctx.trackState(s);

    // Explore standard motions - use directional pruning if enabled
    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotions(s);
    } else {
      explorer.exploreAllStandardMotions(s);
    }

    bool lineIntersectsGoal = targetRange.lineIntersects(pos.line);
    explorer.exploreDirectionalMotions<Forward>(s, lineIntersectsGoal);
  }

  debug("---costMap---");
  map<Pos, double> tempMap(ctx.costMap.begin(), ctx.costMap.end());
  for (auto [state, cost] : tempMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  vector<RangeResult> results;
  int uniqueCount = 0;
  if (params.allowMultiplePerPosition) {
    results = std::move(allResults);
    // Count unique positions in results
    set<Pos> seen;
    for (const auto& r : results) {
      seen.insert({r.getGoalPos().line, r.getGoalPos().col});
    }
    uniqueCount = static_cast<int>(seen.size());
  } else {
    results.reserve(bestResultByPos.size());
    for (auto& [posKey, result] : bestResultByPos) {
      results.push_back(std::move(result));
    }
    uniqueCount = static_cast<int>(results.size());
  }

  MotionSearchStats stats = ctx.getStats(static_cast<int>(results.size()));
  stats.uniquePositionsFound = uniqueCount;

  // Override stop reason for range-specific conditions
  if (uniqueCount >= rangeSize) {
    // Found all positions in range
    stats.stopReason = SearchStopReason::AllResultsFound;
  } else if (static_cast<int>(results.size()) >= params.maxResults) {
    // Hit maxResults limit (only possible with params.allowMultiplePerPosition)
    stats.stopReason = SearchStopReason::MaxResultsFound;
  }

  return RangeMotionResult(std::move(results), stats);
}
