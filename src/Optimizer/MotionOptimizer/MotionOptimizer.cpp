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

CursorPos rangeFirstPos(const CharRange& range) {
  return range.begin;
}

CursorPos rangeFirstPos(const CharLineRange& range) {
  return range.begin;
}

CursorPos rangeFirstPos(const LineCharRange& range) {
  return CursorPos(range.beginLine, 0);
}

CursorPos rangeLastPos(const Lines& lines, const CharRange& range) {
  CursorPos last = lines.getPrevPos(range.end);
  return last.isValid() ? last : range.begin;
}

CursorPos rangeLastPos(const Lines& lines, const CharLineRange& range) {
  CursorPos last = lines.getPrevPos(CursorPos(range.endLine, 0));
  return last.isValid() ? last : range.begin;
}

CursorPos rangeLastPos(const Lines& lines, const LineCharRange& range) {
  CursorPos last = lines.getPrevPos(range.end);
  CursorPos first(range.beginLine, 0);
  return last.isValid() ? last : first;
}

bool isInRange(const CursorPos& pos, const CharRange& range) {
  return pos >= range.begin && pos < range.end;
}

bool isInRange(const CursorPos& pos, const CharLineRange& range) {
  return pos >= range.begin && pos.line < range.endLine;
}

bool isInRange(const CursorPos& pos, const LineCharRange& range) {
  return pos >= CursorPos(range.beginLine, 0) && pos < range.end;
}

bool lineIntersectsTarget(int line, const CharRange& range) {
  if (line < range.begin.line || line > range.end.line) return false;
  if (range.begin.line == range.end.line) {
    return line == range.begin.line && range.end.col > range.begin.col;
  }
  if (line == range.end.line) {
    return range.end.col > 0;
  }
  return true;
}

bool lineIntersectsTarget(int line, const CharLineRange& range) {
  return line >= range.begin.line && line < range.endLine;
}

bool lineIntersectsTarget(int line, const LineCharRange& range) {
  if (line < range.beginLine || line > range.end.line) return false;
  if (line == range.end.line) {
    return range.end.col > 0 || line < range.end.line;
  }
  return true;
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

  // Set range to just be goalPos
  MotionExplorer explorer(ctx, CharRange(goalPos, lines.getNextPos(goalPos)), bufferIndex, 0);

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
    const CharRange& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  BufferIndex localIndex(lines);
  return optimizeToRange(lines, startPos, range, params, userSequence,
                         boundary, navContext, localIndex, 0);
}

RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const CharLineRange& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  BufferIndex localIndex(lines);
  return optimizeToRange(lines, startPos, range, params, userSequence,
                         boundary, navContext, localIndex, 0);
}

RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const LineCharRange& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  BufferIndex localIndex(lines);
  return optimizeToRange(lines, startPos, range, params, userSequence,
                         boundary, navContext, localIndex, 0);
}

// Overloads with caller-provided BufferIndex.
RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const CharRange& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  assert(!isInRange(startPos, range) && "startPos must not be in target range");
  if (startPos < rangeFirstPos(range)) {
    return optimizeToRangeImpl<true>(lines, startPos, range, userSequence,
                                     navContext, boundary, params,
                                     bufferIndex, lineOffset);
  }
  return optimizeToRangeImpl<false>(lines, startPos, range, userSequence,
                                    navContext, boundary, params,
                                    bufferIndex, lineOffset);
}

RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const CharLineRange& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  assert(!isInRange(startPos, range) && "startPos must not be in target range");
  if (startPos < rangeFirstPos(range)) {
    return optimizeToRangeImpl<true>(lines, startPos, range, userSequence,
                                     navContext, boundary, params,
                                     bufferIndex, lineOffset);
  }
  return optimizeToRangeImpl<false>(lines, startPos, range, userSequence,
                                    navContext, boundary, params,
                                    bufferIndex, lineOffset);
}

RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const LineCharRange& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  assert(!isInRange(startPos, range) && "startPos must not be in target range");
  if (startPos < rangeFirstPos(range)) {
    return optimizeToRangeImpl<true>(lines, startPos, range, userSequence,
                                     navContext, boundary, params,
                                     bufferIndex, lineOffset);
  }
  return optimizeToRangeImpl<false>(lines, startPos, range, userSequence,
                                    navContext, boundary, params,
                                    bufferIndex, lineOffset);
}

// Templated implementation - Forward is compile-time constant
// Precondition: bufferIndex/lineOffset are always valid (resolved by public optimizeToRange)
template<bool Forward, class RangeT>
RangeMotionResult MotionOptimizer::optimizeToRangeImpl(
    const Lines& lines,
    const CursorPos& startPos,
    const RangeT& range,
    string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    MotionOptimizerRangeParams params,
    const BufferIndex& bufferIndex,
    int lineOffset) {

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort);

  MotionExplorer explorer(ctx, range, bufferIndex, lineOffset);

  MotionState initialState(startPos, RunningEffort(), 0.0, 0.0);

  map<Pos, RangeResult> bestResultByPos;
  vector<RangeResult> allResults;
  int uniquePositionsFound = 0;

  // Cap effective maxResults at range size (can't find more positions than exist)
  int rangeSize = lines.spanSize(range);
  int effectiveMaxResults = std::min(params.maxResults, rangeSize);
  CursorPos rangeFirst = rangeFirstPos(range);
  CursorPos rangeLast = rangeLastPos(lines, range);

  initialState.setCost(ctx.computePriorityToRange(initialState, rangeFirst, rangeLast));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    CursorPos pos = s.getPos();

    Pos stateKey = s.getKey();
    bool isGoal = isInRange(pos, range);

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

    bool lineIntersectsGoal = lineIntersectsTarget(pos.line, range);
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
