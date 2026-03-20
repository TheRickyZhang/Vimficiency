#include "MotionOptimizer.h"

#include <cassert>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

#include "Effort/EffortBank.h"
#include "MotionHeuristic.h"
#include "MotionExplorer.h"
#include "Optimizer/SearchFrontier.h"
#include "Optimizer/SearchStats.h"

#include "BufferIndex.h"
#include "Types/CharInterval.h"
#include "Types/Pos.h"
#include "Utils/Debug.h"

using namespace std;

using MotionPriorityQueue =
    priority_queue<MotionState, vector<MotionState>, greater<MotionState>>;

MotionResult MotionOptimizer::optimize(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
    MotionOptimizerParams params,
    string_view userSequence,
    const MotionBoundary& boundary,
    const NavContext& navContext) {
  BufferIndex bufferIndex(lines);
  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  CharInterval goalRange(goalPos, goalPos);
  MotionExplorer explorer(lines, navContext, boundary, params, goalRange, bufferIndex, 0);
  EffortBank bank(config);

  MotionPriorityQueue pq;
  unordered_map<Pos, double> costMap;
  MotionSearchStats stats;
  int totalPops = 0;
  const double maxEffort = userEffort * params.exploreFactor;
  auto isInGoalRange = [&](CursorPos pos) {
    return goalRange.containsPos(pos);
  };
  auto scoreState = [&](CursorPos pos, double effort) {
    double distance = MotionHeuristic::distanceToRange(goalRange, pos);
    return params.effortWeight * effort + params.distanceWeight * distance;
  };

  vector<Result> res;
  Pos goalKey(goalPos.line, goalPos.col);
  MotionState initialState(initialPos, RunningEffort(), 0.0, 0.0);
  initialState.setCost(scoreState(initialState.getPos(), initialState.getEffort()));
  pq.push(initialState);
  costMap[initialState.getKey()] = initialState.getCost();

  while (!pq.empty() && totalPops < params.maxNodesPopped) {
    MotionState s = pq.top();
    pq.pop();
    totalPops++;

    Pos stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);

    if (isGoal) {
      stats.incrementNodesExplored();
      res.emplace_back(s.getSequence().str(), s.getRunningEffort().getEffort(config));
      if (res.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    }

    auto it = costMap.find(s.getKey());
    if (it != costMap.end() && it->second < s.getCost()) {
      stats.incrementStatesSkipped();
      continue;
    }

    stats.incrementNodesExplored();
    debug("\"" + s.getSequence().str() + "\"", s.getCost());
    CursorPos pos = s.getPos();
    stats.maybeRecordExploredState(pos.line, pos.col, s.getEffort(), s.getSequence());

    auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
          MotionState next = s.afterMotion(ks, bank[motionId], endpoint, config, scoreState);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onCounted = [&](KSId motionId, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double extraPenalty) {
          MotionState next = s.afterCountedMotion(
              ks, count, endpoint, config, extraPenalty, scoreState);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
          MotionState next = s.afterFMotion(motion, newCol, config, scoreState);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };

    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotions(s, onStatic);
    } else {
      explorer.exploreAllStandardMotions(s, onStatic);
    }
    explorer.exploreCountedMotions(s, onCounted, onFMotion);
  }

  debug("---costMap---");
  for (auto [state, cost] : costMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  stats.finalizeSingleGoal(
      static_cast<int>(res.size()),
      static_cast<int>(pq.size()),
      params.maxResults,
      params.maxNodesPopped,
      totalPops,
      pq.empty());

  return MotionResult(std::move(res), stats);
}

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

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  int rangeSize = lines.spanSize(range);
  int maxResults = min(params.maxResults, rangeSize);

  MotionExplorer explorer(lines, navContext, boundary, params, range, bufferIndex, lineOffset);
  EffortBank bank(config);

  MotionPriorityQueue pq;
  unordered_map<Pos, double> costMap;
  MotionSearchStats stats;
  int totalPops = 0;
  const double maxEffort = userEffort * params.exploreFactor;
  
  auto scoreState = [&](CursorPos pos, double effort) {
    double distance = MotionHeuristic::distanceToRange(range, pos);
    return params.effortWeight * effort + params.distanceWeight * distance;
  };

  auto isInGoalRange = [&](CursorPos pos) {
    return range.containsPos(pos);
  };

  MotionState initialState(startPos, RunningEffort(), 0.0, 0.0);
  initialState.setCost(scoreState(initialState.getPos(), initialState.getEffort()));
  pq.push(initialState);
  costMap[initialState.getKey()] = initialState.getCost();

  unordered_map<Pos, RangeResult> bestResultByPos;
  vector<RangeResult> allResults;
  [[maybe_unused]] set<Pos> uniquePositionsSeen;

  while (!pq.empty() && totalPops < params.maxNodesPopped) {
    MotionState s = pq.top(); pq.pop();
    totalPops++;

    CursorPos pos = s.getPos();
    Pos stateKey = s.getKey();
    bool isGoal = range.containsPos(pos);

    if (isGoal) {
      stats.incrementNodesExplored();
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
    }

    auto it = costMap.find(s.getKey());
    if (it != costMap.end() && it->second < s.getCost()) {
      stats.incrementStatesSkipped();
      continue;
    }

    stats.incrementNodesExplored();
    debug("\"" + s.getSequence().str() + "\"", s.getCost());
    stats.maybeRecordExploredState(pos.line, pos.col, s.getEffort(), s.getSequence());

    auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
          MotionState next = s.afterMotion(ks, bank[motionId], endpoint, config, scoreState);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onCounted = [&](KSId motionId, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double extraPenalty) {
          MotionState next = s.afterCountedMotion(
              ks, count, endpoint, config, extraPenalty, scoreState);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
          MotionState next = s.afterFMotion(motion, newCol, config, scoreState);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotions(s, onStatic);
    } else {
      explorer.exploreAllStandardMotions(s, onStatic);
    }
    explorer.exploreCountedMotions(s, onCounted, onFMotion);
  }

  debug("---costMap---");
  map<Pos, double> tempMap(costMap.begin(), costMap.end());
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

  stats.finalizeRangeGoal(
      static_cast<int>(results.size()),
      uniqueCount,
      static_cast<int>(pq.size()),
      rangeSize,
      params.maxResults,
      params.maxNodesPopped,
      totalPops,
      pq.empty());

  return RangeMotionResult(std::move(results), stats);
}
