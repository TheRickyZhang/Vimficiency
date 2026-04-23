#include "MotionFrontier.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Effort/EffortBank.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/MotionOptimizer/BufferIndex.h"
#include "Optimizer/MotionOptimizer/MotionExplorer.h"
#include "Optimizer/MotionOptimizer/MotionHeuristic.h"
#include "Optimizer/MotionOptimizer/MotionOptimizerParams.h"
#include "Optimizer/MotionOptimizer/MotionRangeConversion.h"
#include "Optimizer/MotionOptimizer/MotionState.h"

using namespace std;

namespace {

MotionOptimizerParams makeSingleStepParams() {
  CompositionOptimizerParams comp;
  MotionOptimizerParams p;
  p.withFMotionThreshold(comp.fMotionThreshold)
   .withDirectionalPruning(comp.useDirectionalPruning)
   .withLinePaddingAbove(comp.motionPaddingAbove)
   .withLinePaddingBelow(comp.motionPaddingBelow)
   .withMinCountRepeat(comp.minPrefixCount)
   .withMaxCountRepeat(comp.maxPrefixCount);
  return p;
}

}  // namespace

vector<MotionFrontierItem> rankMotionFrontier(
    const MotionFrontierQuery& query,
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

  // Explore shows IMMEDIATE NEXT MOLECULES. Peek the live A* frontier at
  // depth 1 from the cursor — i.e. enumerate all candidate single-step
  // transitions, score each by `effort + heuristic(target)` using the
  // same scoring the full optimizer would, sort, and take top K. No full
  // search to the goal.
  MotionOptimizerParams params = makeSingleStepParams();
  BufferIndex bufferIndex(query.lines);
  MotionExplorer explorer(query.lines, query.navContext, query.boundary,
                          params, *motionRange, bufferIndex, 0);
  EffortBank bank(config);

  auto scoreState = [&](CursorPos pos, double effort) {
    const double distance = MotionHeuristic::distanceToRange(*motionRange, pos);
    return params.effortWeight * effort + params.distanceWeight * distance;
  };

  MotionState base(query.cursor, RunningEffort(), 0.0, 0.0);
  base.setCost(scoreState(base.getPos(), base.getEffort()));

  // No-op successors (landing == cursor) make no progress; filter at
  // emission. The full A* naturally filters via its cost map, but at
  // depth 1 there is no prior-best cost to compare against.
  auto isNoOp = [&](CursorPos endpoint) { return endpoint == query.cursor; };
  auto landingKey = [](CursorPos p) {
    return (static_cast<int64_t>(p.line) << 32) | static_cast<uint32_t>(p.col);
  };

  // Two collection strategies.
  //
  // Default (`allowMultiplePerPosition == false`): compare-and-replace per
  // landing during emission — keep only the cheapest molecule reaching each
  // cell. Honest both on memory (one state per landing) and on compute (we
  // never build a `successors` vector we'll mostly discard).
  //
  // Opt-in (`allowMultiplePerPosition == true`): accumulate every molecule
  // and dedup only by literal sequence text — surfaces `w` / `W` / `e` all
  // landing on the same cell as distinct recommendations.
  vector<MotionState> successors;
  unordered_map<int64_t, MotionState> bestByLanding;

  auto emit = [&](MotionState s) {
    if (query.allowMultiplePerPosition) {
      successors.push_back(std::move(s));
      return;
    }
    int64_t key = landingKey(s.getPos());
    auto it = bestByLanding.find(key);
    if (it == bestByLanding.end()) {
      bestByLanding.emplace(key, std::move(s));
    } else if (s.getCost() < it->second.getCost()) {
      it->second = std::move(s);
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

  if (!query.allowMultiplePerPosition) {
    successors.reserve(bestByLanding.size());
    for (auto& [_, s] : bestByLanding) successors.push_back(std::move(s));
  }
  sort(successors.begin(), successors.end());

  vector<MotionFrontierItem> items;
  items.reserve(static_cast<size_t>(query.maxCount));
  unordered_set<string> seenSeq;  // only used in allow-multiple mode
  for (const MotionState& s : successors) {
    string seq = s.getSequence().str();
    if (seq.empty()) continue;
    if (query.allowMultiplePerPosition) {
      if (!seenSeq.insert(seq).second) continue;
    }
    items.push_back(MotionFrontierItem{
        .molecule = std::move(seq),
        .landingPos = s.getPos(),
        .cost = s.getEffort(),
    });
    if (static_cast<int>(items.size()) >= query.maxCount) break;
  }
  return items;
}

vector<MotionFrontierItem> rankMotionFrontierToLine(
    const Lines& lines,
    CursorPos cursor,
    int targetLine,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const Config& config,
    int maxCount) {
  if (targetLine < 0 || targetLine >= static_cast<int>(lines.size())) return {};
  return rankMotionFrontier(
      MotionFrontierQuery{
          .lines = lines,
          .cursor = cursor,
          .targetRange = CharRange(
              CursorPos(targetLine, 0),
              CursorPos(targetLine, lines[targetLine].effectiveSize())),
          .boundary = boundary,
          .navContext = navContext,
          .maxCount = maxCount,
      },
      config);
}
