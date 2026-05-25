#include "NavFrontier.h"

#include <algorithm>
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
#include "Optimizer/NavOptimizer/NavState.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Utils/Debug.h"

#include <unordered_set>

using namespace std;

vector<Suggestion> rankNavFrontier(
    const NavFrontierQuery& query,
    const Config& config) {
  // Convert from range -> interval
  const CharInterval motionRange(query.targetRange, query.lines);

  NavOptimizerParams params = OptimizerParamOverrides::resolved<NavOptimizerParams>(query.overrides);

  BufferIndex bufferIndex(query.lines);
  NavExplorer explorer(query.lines, query.navContext, query.boundary,
                          params, motionRange, bufferIndex, 0);

  // Would it be faster to include all of these in the query, assuming that config will be stable per explore invocation?
  EffortBank bank(config);
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort(config);

  auto scoreState = [&](CursorPos pos, double effort) {
    const double distance = NavHeuristic::distanceToRange(motionRange, pos);
    return params.effortWeight * effort + params.distanceWeight * distance;
  };
  auto sortValue = [&](const NavState& s) {
    const double effortDiff = s.getEffort() - acceptedCost;
    const double distance = NavHeuristic::distanceToRange(motionRange, s.getPos());
    switch (query.sortMode) {
      case SuggestionSortMode::Effort:
        return effortDiff;
      case SuggestionSortMode::Distance:
        return distance;
      case SuggestionSortMode::Score:
        return params.effortWeight * effortDiff + params.distanceWeight * distance;
    }
    return effortDiff;
  };

  NavStateFactory states(config, scoreState);
  NavState base = states.initial(query.cursor, acceptedEffort);

  // Cap up to `params.maxResultsPerEndPos` distinct tokens per landing
  // cell. Default 1 keeps just the cheapest token per cell; values > 1
  // surface multiple distinct paths to the same cell (e.g. `w` / `W` /
  // `e` all reaching the same word start). Already clamped to ≥1 above.
  const int cap = params.maxResultsPerEndPos;
  unordered_map<Pos, vector<NavState>> resultsByLanding;

  auto emit = [&](NavState s) {
    auto& bucket = resultsByLanding[s.getPos().pos()];
    if (static_cast<int>(bucket.size()) < cap) {
      bucket.push_back(std::move(s));
      return;
    }
    // Bucket is full — replace the worst entry if the new one is cheaper.
    auto worst = std::max_element(bucket.begin(), bucket.end(),
        [&](const NavState& a, const NavState& b) {
          return sortValue(a) < sortValue(b);
        });
    if (sortValue(s) < sortValue(*worst)) {
      *worst = std::move(s);
    }
  };

  auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
    emit(states.afterMotion(base, ks, bank[motionId], endpoint));
  };
  auto onCounted = [&](KSId /*motionId*/, const KeyedSequence& ks, int count,
                       CursorPos endpoint, double extraPenalty) {
    emit(states.afterCountedMotion(base, ks, count, endpoint, extraPenalty));
  };
  auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
    emit(states.afterFMotion(base, motion, newCol));
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
  stable_sort(successors.begin(), successors.end(),
              [&](const NavState& a, const NavState& b) {
                const double av = sortValue(a);
                const double bv = sortValue(b);
                if (av != bv) return av < bv;
                return a.getSequence().str() < b.getSequence().str();
              });

  vector<Suggestion> items;
  items.reserve(successors.size());
  for (const NavState& s : successors) {
    string seq = s.getSequence().str();
    if (seq.empty()) continue;
    const double effortDiff = s.getEffort() - acceptedCost;
    const double distance = NavHeuristic::distanceToRange(motionRange, s.getPos());
    Suggestion suggestion{
        .token = Token{std::move(seq)},
        .landingPos = s.getPos(),
    };
    updateSuggestionMetrics(
        suggestion, effortDiff, distance,
        params.effortWeight, params.distanceWeight);
    items.push_back(std::move(suggestion));
  }
  sortAndCapSuggestions(items, query.maxCount, query.sortMode);

  // Pairwise-distinct tokens. Per-cell dedup above bounds each landing cell,
  // but a global CHECK guards against any future enumeration overlap.
  unordered_set<string> seen;
  for (const Suggestion& s : items) {
    CHECK(seen.insert(string(s.token)).second,
          "duplicate NavFrontier token; fix enumerator overlap");
  }
  return items;
}
