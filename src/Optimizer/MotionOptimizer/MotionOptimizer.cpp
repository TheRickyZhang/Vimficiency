#include "MotionOptimizer.h"

#include <limits>
#include <map>
#include <set>

#include "MotionSearchContext.h"
#include "MotionExplorer.h"

#include "BufferIndex.h"
#include "Types/CharInterval.h"
#include "Types/Pos.h"
#include "Utils/Debug.h"

using namespace std;

MotionResult MotionOptimizer::optimize(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
    MotionOptimizerParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  // Initial setup
  BufferIndex bufferIndex(lines);
  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);
  // debug("user effort for sequence", userSequence, "is", userEffort);

  CharInterval goalRange(goalPos, goalPos);
  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort, goalRange);
  MotionExplorer explorer(ctx, bufferIndex, 0);

  vector<Result> res;
  Pos goalKey(goalPos.line, goalPos.col);
  MotionState initialState(initialPos, RunningEffort(), 0.0, 0.0);
  initialState.setCost(ctx.computePriorityToGoal(initialState, goalPos));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  // Main A* loop
  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();

    CursorPos pos = s.getPos();
    Pos stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);

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

    // For debuggin
    debug("\"" + s.getSequence().str() + "\"", s.getCost());
    ctx.trackState(s);

    // Explore standard motions - use directional pruning if enabled
    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotions(s);
    } else {
      explorer.exploreAllStandardMotions(s);
    }

    // Explore count-based word/paragraph/sentence and f/F if same line
    explorer.exploreCountedMotions(s);
  }

  debug("---costMap---");
  for (auto [state, cost] : ctx.costMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  MotionSearchStats stats = ctx.getStats(static_cast<int>(res.size()));
  return MotionResult(std::move(res), stats);
}

// =================================================
// optimizeToRange implementation
// =================================================

RangeMotionResult MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const CursorPos& startPos,
    const CharInterval& range,
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
    const CharInterval& range,
    MotionOptimizerRangeParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  assert(range.isValid() && "target interval must be non-empty");
  assert(!range.containsPos(startPos) && "startPos must not be in target interval");
  // Initial setup
  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  int rangeSize = lines.spanSize(range);
  int maxResults = std::min(params.maxResults, rangeSize);

  MotionSearchContext ctx(lines, navContext, boundary, params, config, userEffort, range);
  MotionExplorer explorer(ctx, bufferIndex, lineOffset);

  MotionState initialState(startPos, RunningEffort(), 0.0, 0.0);

  unordered_map<Pos, RangeResult> bestResultByPos;
  vector<RangeResult> allResults;
  set<Pos> uniquePositionsSeen;  // Only populated when allowMultiplePerPosition

  initialState.setCost(ctx.computePriorityToRange(initialState));
  ctx.pq.push(initialState);
  ctx.costMap[initialState.getKey()] = initialState.getCost();

  while (ctx.shouldContinue()) {
    MotionState s = ctx.popNext();
    CursorPos pos = s.getPos();

    Pos stateKey = s.getKey();
    bool isGoal = range.containsPos(pos);

    if (isGoal) {
      ctx.markProcessed();
      double effort = s.getRunningEffort().getEffort(config);

      if (params.allowMultiplePerPosition) {
        allResults.emplace_back(s.getSequence().str(), effort, pos);
        uniquePositionsSeen.insert(stateKey);
        if (allResults.size() >= static_cast<size_t>(params.maxResults)) {
          debug("optimizeToRange: max results reached");
          break;
        }
      } else {
        auto it = bestResultByPos.find(stateKey);
        if (it == bestResultByPos.end()) {
          bestResultByPos.emplace(stateKey, RangeResult(s.getSequence().str(), effort, pos));
          if (static_cast<int>(bestResultByPos.size()) >= maxResults) {
            debug("optimizeToRange: max unique positions reached (", bestResultByPos.size(), "/", rangeSize, ")");
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

    explorer.exploreCountedMotions(s);
  }

  debug("---costMap---");
  map<Pos, double> tempMap(ctx.costMap.begin(), ctx.costMap.end());
  for (auto [state, cost] : tempMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  vector<RangeResult> results;
  int uniqueCount;
  if (params.allowMultiplePerPosition) {
    results = std::move(allResults);
    uniqueCount = static_cast<int>(uniquePositionsSeen.size());
  } else {
    results.reserve(bestResultByPos.size());
    for (auto& [posKey, result] : bestResultByPos) {
      results.push_back(std::move(result));
    }
    uniqueCount = static_cast<int>(bestResultByPos.size());
  }

  MotionSearchStats stats = ctx.getRangeStats(static_cast<int>(results.size()), uniqueCount, rangeSize);
  return RangeMotionResult(std::move(results), stats);
}
