#include "NavFrontier.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Effort/EffortBank.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/NavOptimizer/BufferIndex.h"
#include "Optimizer/NavOptimizer/NavExplorer.h"
#include "Optimizer/NavOptimizer/NavHeuristic.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Optimizer/NavOptimizer/NavState.h"
#include "Optimizer/OptimizerParamOverrides.h"

using namespace std;

vector<Suggestion> rankNavFrontier(
    const NavFrontierQuery& query,
    const Config& config) {
  if (query.maxCount <= 0) return {};
  if (query.targetRange.containsPos(query.cursor)) return {};

  // Pure-insertion targets are zero-width in CharRange terms but still a
  // reachable point. A cursor already at the insertion point has no
  // motion-frontier work; the half-open containsPos check doesn't catch
  // it, so guard explicitly.
  optional<CharInterval> motionRange;
  if (query.targetRange.isEmpty()) {
    if (query.cursor == query.targetRange.begin) return {};
    motionRange = CharInterval(query.targetRange.begin, query.targetRange.begin);
  } else {
    motionRange = tryToMotionInterval(query.lines, query.targetRange);
  }
  if (!motionRange) return {};

  // Explore shows IMMEDIATE NEXT TOKENS. Peek the live A* frontier at
  // depth 1 from the cursor — i.e. enumerate all candidate single-step
  // transitions, score each by `effort + heuristic(target)` using the
  // same scoring the full optimizer would, sort, and take top K. No full
  // search to the goal.
  NavOptimizerParams params;
  if (query.overrides) query.overrides->applyTo(params);
  // Guard: per-cell cap must be ≥ 1 even if overrides set 0/negative.
  params.maxResultsPerEndPos = std::max(1, params.maxResultsPerEndPos);
  BufferIndex bufferIndex(query.lines);
  NavExplorer explorer(query.lines, query.navContext, query.boundary,
                          params, *motionRange, bufferIndex, 0);
  EffortBank bank(config);
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort(config);

  auto scoreState = [&](CursorPos pos, double effort) {
    const double distance = NavHeuristic::distanceToRange(*motionRange, pos);
    return params.effortWeight * effort + params.distanceWeight * distance;
  };

  NavState base(query.cursor, acceptedEffort, acceptedCost, 0.0);
  base.setCost(scoreState(base.getPos(), base.getEffort()));

  // No-op successors (landing == cursor) make no progress; filter at
  // emission. The full A* naturally filters via its cost map, but at
  // depth 1 there is no prior-best cost to compare against.
  auto isNoOp = [&](CursorPos endpoint) { return endpoint == query.cursor; };
  auto landingKey = [](CursorPos p) {
    return (static_cast<int64_t>(p.line) << 32) | static_cast<uint32_t>(p.col);
  };

  // Cap up to `params.maxResultsPerEndPos` distinct tokens per landing
  // cell. Default 1 keeps just the cheapest token per cell; values > 1
  // surface multiple distinct paths to the same cell (e.g. `w` / `W` /
  // `e` all reaching the same word start). Already clamped to ≥1 above.
  const int cap = params.maxResultsPerEndPos;
  unordered_map<int64_t, vector<NavState>> resultsByLanding;

  auto emit = [&](NavState s) {
    int64_t key = landingKey(s.getPos());
    auto& bucket = resultsByLanding[key];
    if (static_cast<int>(bucket.size()) < cap) {
      bucket.push_back(std::move(s));
      return;
    }
    // Bucket is full — replace the worst entry if the new one is cheaper.
    auto worst = std::max_element(bucket.begin(), bucket.end());
    if (s.getCost() < worst->getCost()) {
      *worst = std::move(s);
    }
  };

  auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
    if (isNoOp(endpoint)) return;
    emit(base.afterMotion(ks, bank[motionId], endpoint, config, scoreState));
  };
  auto onCounted = [&](KSId /*motionId*/, const KeyedSequence& ks, int count,
                       CursorPos endpoint, double extraPenalty) {
    if (isNoOp(endpoint)) return;
    emit(base.afterCountedMotion(ks, count, endpoint, config, extraPenalty, scoreState));
  };
  auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
    if (newCol == query.cursor.col) return;
    emit(base.afterFMotion(motion, newCol, config, scoreState));
  };

  if (params.useDirectionalPruning) {
    explorer.exploreDirectionalStandardMotions(base, onStatic);
  } else {
    explorer.exploreAllStandardMotions(base, onStatic);
  }
  explorer.exploreCountedMotions(base, onCounted, onFMotion);

  // Flatten cap-N buckets into a single sorted successor list.
  vector<NavState> successors;
  for (auto& [_, bucket] : resultsByLanding) {
    for (auto& s : bucket) successors.push_back(std::move(s));
  }
  sort(successors.begin(), successors.end());

  vector<Suggestion> items;
  items.reserve(static_cast<size_t>(query.maxCount));
  for (const NavState& s : successors) {
    string seq = s.getSequence().str();
    if (seq.empty()) continue;
    items.push_back(Suggestion{
        .token = Token{std::move(seq)},
        .landingPos = s.getPos(),
        .costDiff = s.getEffort() - acceptedCost,
    });
    if (static_cast<int>(items.size()) >= query.maxCount) break;
  }
  return items;
}
