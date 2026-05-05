#include "NavOptimizer.h"

#include <cassert>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

#include "Effort/EffortBank.h"
#include "NavHeuristic.h"
#include "NavExplorer.h"
#include "Optimizer/SearchFrontier.h"
#include "Optimizer/SearchStats.h"

#include "BufferIndex.h"
#include "Types/CharInterval.h"
#include "Types/Pos.h"
#include "Utils/Debug.h"

using namespace std;

using NavPriorityQueue =
    priority_queue<NavState, vector<NavState>, greater<NavState>>;

LandingNavResult NavOptimizer::optimize(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
    NavOptimizerParams params,
    string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navContext) {
  return optimize(lines, initialPos, CharInterval(goalPos, goalPos),
                  params, userSequence, boundary, navContext);
}

LandingNavResult NavOptimizer::optimize(
    const Lines& lines,
    const CursorPos& startPos,
    const CharInterval& range,
    NavOptimizerParams params,
    string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navContext) {
  BufferIndex localIndex(lines);
  return optimize(lines, startPos, range, params, userSequence,
                  boundary, navContext, localIndex, 0);
}

LandingNavResult NavOptimizer::optimize(
    const Lines& lines,
    const CursorPos& startPos,
    const CharInterval& range,
    NavOptimizerParams params,
    string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  assert(range.isValid() && "target interval must be non-empty");

  // startPos already in the goal interval — nothing to do, the empty motion
  // sequence is the answer.
  if (range.containsPos(startPos)) {
    NavSearchStats stats;
    std::vector<LandingResult> results;
    results.emplace_back(std::string{}, 0.0, startPos);
    stats.finalizeRangeGoal(
        static_cast<int>(results.size()),
        /*uniquePositions=*/1,
        /*queueRemaining=*/0,
        lines.spanSize(range),
        params.maxResults,
        params.maxNodesPopped,
        /*totalPops=*/0,
        /*pqEmpty=*/true);
    return LandingNavResult(std::move(results), stats);
  }

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);

  int rangeSize = lines.spanSize(range);
  int maxResults = min(params.maxResults, rangeSize);

  NavExplorer explorer(lines, navContext, boundary, params, range, bufferIndex, lineOffset);
  EffortBank bank(config);

  NavPriorityQueue pq;
  unordered_map<Pos, double> costMap;
  NavSearchStats stats;
  int totalPops = 0;
  const double maxEffort = userEffort * params.exploreFactor;

  auto scoreState = [&](CursorPos pos, double effort) {
    double distance = NavHeuristic::distanceToRange(range, pos);
    return params.effortWeight * effort + params.distanceWeight * distance;
  };

  auto isInGoalRange = [&](CursorPos pos) {
    return range.containsPos(pos);
  };

  NavStateFactory states(config, scoreState);
  NavState initialState = states.initial(startPos);
  pq.push(initialState);
  costMap[initialState.getKey()] = initialState.getCost();

  unordered_map<Pos, LandingResult> bestResultByPos;
  vector<LandingResult> allResults;
  [[maybe_unused]] set<Pos> uniquePositionsSeen;

  while (!pq.empty() && totalPops < params.maxNodesPopped) {
    NavState s = pq.top(); pq.pop();
    totalPops++;

    CursorPos pos = s.getPos();
    Pos stateKey = s.getKey();
    bool isGoal = range.containsPos(pos);

    if (isGoal) {
      stats.incrementNodesExplored();
      double effort = s.getRunningEffort().getEffort(config);

      if ((params.maxResultsPerEndPos > 1)) {
        allResults.emplace_back(s.getSequence().str(), effort, pos);
        uniquePositionsSeen.insert(stateKey);
        if (allResults.size() >= static_cast<size_t>(params.maxResults)) {
          debug("optimize: max results reached");
          break;
        }
      } else {
        auto it = bestResultByPos.find(stateKey);
        if (it == bestResultByPos.end()) {
          bestResultByPos.emplace(stateKey, LandingResult(s.getSequence().str(), effort, pos));
          if (static_cast<int>(bestResultByPos.size()) >= maxResults) {
            debug("optimize: max unique positions reached (", bestResultByPos.size(), "/", rangeSize, ")");
            break;
          }
        } else if (effort < it->second.getCost()) {
          it->second = LandingResult(s.getSequence().str(), effort, pos);
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
          NavState next = states.afterMotion(s, ks, bank[motionId], endpoint);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onCounted = [&](KSId /*motionId*/, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double extraPenalty) {
          NavState next = states.afterCountedMotion(s, ks, count, endpoint, extraPenalty);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
          NavState next = states.afterFMotion(s, motion, newCol);
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

  vector<LandingResult> results;
  int uniqueCount;
  if ((params.maxResultsPerEndPos > 1)) {
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

  return LandingNavResult(std::move(results), stats);
}
