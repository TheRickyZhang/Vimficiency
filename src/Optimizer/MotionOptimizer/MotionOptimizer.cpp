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

// Public entry point - dispatches to templated implementation based on direction
MotionResult MotionOptimizer::optimize(
    const Lines& lines,
    const CursorPos& startPos,
    const CursorPos& endPos,
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
    const CursorPos& startPos,
    const RunningEffort& startingEffort,
    const CursorPos &endPos,
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

  // Create point range [endPos, nextPos) for the unified MotionExplorer
  CursorPos rangeEnd = lines.getNextPosUnclamped(endPos);
  MotionExplorer explorer(ctx, endPos, rangeEnd, bufferIndex, 0);

  Pos goalKey(endPos.line, endPos.col);

  MotionState initialState(startPos, startingEffort, 0.0, 0.0);
  vector<Result> res;

  initialState.setCost(ctx.computePriorityToGoal(initialState, endPos));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    CursorPos pos = s.getPos();

    Pos stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);
    bool isSameLine = (pos.line == endPos.line);

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

  SearchStats stats = ctx.getStats(static_cast<int>(res.size()));
  return {.results = std::move(res), .stats = stats};
}

// Explicit template instantiations
template MotionResult MotionOptimizer::optimizeImpl<true>(
    const Lines&, const CursorPos&, const RunningEffort&, const CursorPos&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerParams);
template MotionResult MotionOptimizer::optimizeImpl<false>(
    const Lines&, const CursorPos&, const RunningEffort&, const CursorPos&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerParams);

// =================================================
// optimizeToRange implementation
// =================================================

// Simple bufferIndex forwarder
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const CursorPos& rangeBegin,
    const CursorPos& rangeEnd,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const RunningEffort& startingEffort,
    const NavContext& navContext) {
  BufferIndex localIndex(lines);
  return optimizeToRange(lines, startPos, rangeBegin, rangeEnd, params, userSequence,
                         boundary, startingEffort, navContext, BufferIndexRef(localIndex, 0));
}

// Minimal overload with caller-provided BufferIndex
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const CursorPos& rangeBegin,
    const CursorPos& rangeEnd,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const RunningEffort& startingEffort,
    const NavContext& navContext,
    BufferIndexRef bufferRef) {
  assert(!(startPos >= rangeBegin && startPos < rangeEnd) &&
         "startPos must not be in [rangeBegin, rangeEnd)");

  if (startPos < rangeBegin)
    return optimizeToRangeImpl<true>(lines, startPos, startingEffort, rangeBegin, rangeEnd,
                                     userSequence, navContext, boundary, params,
                                     bufferRef.index, bufferRef.indexLineOffset);
  else
    return optimizeToRangeImpl<false>(lines, startPos, startingEffort, rangeBegin, rangeEnd,
                                      userSequence, navContext, boundary, params,
                                      bufferRef.index, bufferRef.indexLineOffset);
}

// Templated implementation - Forward is compile-time constant
// Precondition: bufferIndex/lineOffset are always valid (resolved by public optimizeToRange)
template<bool Forward>
RangeMotionResult MotionOptimizer::optimizeToRangeImpl(
    const Lines& lines,
    const CursorPos& startPos,
    const RunningEffort& startingEffort,
    const CursorPos& rangeBegin,
    const CursorPos& rangeEnd,
    string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerRangeParams params,
    const BufferIndex& bufferIndex,
    int bufferIndexLineOffset) {

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);

  MotionExplorer explorer(ctx, rangeBegin, rangeEnd, bufferIndex, bufferIndexLineOffset);

  MotionState initialState(startPos, startingEffort, 0.0, 0.0);

  // Cap effective maxResults at range size (can't find more positions than exist)
  int rangeSize = lines.spanSize(rangeBegin, rangeEnd);
  int effectiveMaxResults = std::min(params.maxResults, rangeSize);

  vector<RangeResult> bestResults(rangeSize);
  vector<RangeResult> allResults;
  int uniquePositionsFound = 0;
  // Convert half-open rangeEnd to inclusive rangeLast for distance/priority computation.
  Pos rangeLast = lines.getPrevPos(rangeEnd);
  if (!rangeLast.isValid()) {
    rangeLast = Pos(rangeBegin.line, rangeBegin.col);
  }

  initialState.setCost(ctx.computePriorityToRange(initialState, rangeBegin, rangeLast));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    CursorPos pos = s.getPos();

    Pos stateKey = s.getKey();
    bool isGoal = (pos >= rangeBegin && pos < rangeEnd);

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
        int idx = lines.spanSize(rangeBegin, pos);
        if (!bestResults[idx].isValid()) {
          bestResults[idx] = RangeResult(s.getSequence().str(), effort, pos);
          uniquePositionsFound++;
          if (uniquePositionsFound >= effectiveMaxResults) {
            debug("optimizeToRange: max unique positions reached (", uniquePositionsFound, "/", rangeSize, ")");
            break;
          }
        } else if (effort < bestResults[idx].keyCost) {
          bestResults[idx] = RangeResult(s.getSequence().str(), effort, pos);
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

    // Explore counted motions (Forward is compile-time constant).
    // A line is in [rangeBegin, rangeEnd) iff:
    // - it's strictly between begin/end lines, or
    // - it's the begin line, or
    // - it's the end line and rangeEnd has positive column.
    bool isSameLine = false;
    if (rangeBegin.line == rangeEnd.line) {
      isSameLine = (pos.line == rangeBegin.line && rangeEnd.col > rangeBegin.col);
    } else if (pos.line >= rangeBegin.line && pos.line <= rangeEnd.line) {
      isSameLine = (pos.line < rangeEnd.line) || (rangeEnd.col > 0);
    }
    explorer.exploreDirectionalMotions<Forward>(s, isSameLine);
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
      seen.insert({r.goalPos.line, r.goalPos.col});
    }
    uniqueCount = static_cast<int>(seen.size());
  } else {
    results.reserve(uniquePositionsFound);
    for (auto& r : bestResults) {
      if (r.isValid()) {
        results.push_back(std::move(r));
      }
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
    const Lines&, const CursorPos&, const RunningEffort&, const CursorPos&, const CursorPos&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerRangeParams,
    const BufferIndex&, int);
template RangeMotionResult MotionOptimizer::optimizeToRangeImpl<false>(
    const Lines&, const CursorPos&, const RunningEffort&, const CursorPos&, const CursorPos&,
    string_view, const NavContext&, const MotionBoundary&, MotionOptimizerRangeParams,
    const BufferIndex&, int);
