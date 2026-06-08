#include "CompositionFrontier.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "Effort/RunningEffort.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/CompositionOptimizer/CompositionStrategies.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"
#include "Optimizer/CompositionOptimizer/PlannedEditArtifacts.h"
#include "Optimizer/FrontierCommon.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Types/Lines.h"
#include "Utils/Debug.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

namespace {

// Same single-token + duplicate-CHECK shape as TransformFrontier::EditEmitter.
// Each emitting site below must produce a token disjoint from every other; a
// duplicate is a programmer error and aborts in release.
struct CompositionEmitter {
  vector<Suggestion>& items;
  unordered_set<string> emittedTokens;
  const RunningEffort& acceptedEffort;
  double acceptedCost;
  double effortWeight;
  double distanceWeight;
  const Config& config;

  double costDiffFor(string_view token) const {
    RunningEffort candidate(globalSequenceToKeys().tokenize(token), config);
    RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate);
    return merged.getEffort(config) - acceptedCost;
  }

  void emit(string_view token, CursorPos landingPos, double distance = 0.0) {
    Suggestion suggestion{
        .token = Token{token},
        .landingPos = landingPos,
    };
    updateSuggestionMetrics(
        suggestion, costDiffFor(token), distance,
        effortWeight, distanceWeight);
    CHECK(emittedTokens.insert(string(suggestion.token)).second,
          "duplicate CompositionFrontier token; fix enumerator overlap");
    items.push_back(std::move(suggestion));
  }
};

bool cursorInInsertionRange(CursorPos cursor, int targetLine,
                            int beginCol, int endCol) {
  return cursor.line == targetLine &&
         cursor.col >= beginCol && cursor.col < endCol;
}

void emitInsertionStrategy(const CompositionFrontierQuery& query,
                           const CompositionStrategies::Insertion& s,
                           CompositionEmitter& emitter) {
  if (!cursorInInsertionRange(query.cursor, s.targetLine, s.beginCol, s.endCol)) {
    return;
  }
  emitter.emit(s.structural, s.goalPos);
}

void emitBracketQuoteStrategy(const CompositionFrontierQuery& query,
                              const CompositionStrategies::BracketQuote& s,
                              CompositionEmitter& emitter) {
  if (query.cursor.line != s.line || query.cursor.col != s.col) return;
  emitter.emit(s.structural, s.goalPos);
}

double diffDistance(const DiffState& diff) {
  return textDistanceEstimate(diff.deletedLines()) +
         textDistanceEstimate(diff.insertedLines());
}

double residualDistanceAfter(const Lines& before,
                             const Lines& afterStep,
                             const DiffState& diff) {
  Lines target = MyersDiff::applyDiffState(diff, before);
  auto residual = DiffText::calculateContiguousResidualDiff(afterStep, target);
  if (!residual) return 0.0;
  return textDistanceEstimate(residual->deletedLines()) +
         textDistanceEstimate(residual->insertedLines());
}

// Bare `J` is composition-emitted from any column on the join entry line, as
// long as one `J` makes progress on the diff without exactly reaching the
// post-edit fencepost. The fencepost-equal case is owned by TransformFrontier's
// explorer J lane, so this stays disjoint with that lane too.
void emitJoinAction(const CompositionFrontierQuery& query,
                    const JoinPlan& joinPlan,
                    CompositionEmitter& emitter) {
  if (query.cursor.line != joinPlan.entryLine) return;
  if (query.cursor.line + 1 >= static_cast<int>(query.lines.size())) return;

  Lines afterLines = query.lines;
  CursorPos afterPos = query.cursor;
  VimCore::joinLines(afterLines, afterPos, /*addSpace=*/true);
  if (afterLines == query.lines) return;
  if (afterLines == MyersDiff::applyDiffState(query.diff, query.lines)) return;

  double remaining = residualDistanceAfter(query.lines, afterLines, query.diff);
  if (remaining >= diffDistance(query.diff)) return;

  emitter.emit("J", afterPos, remaining);
}

}  // namespace

vector<Suggestion> rankCompositionFrontier(
    const CompositionFrontierQuery& query,
    const Config& config) {
  vector<Suggestion> items;
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort(config);
  CompositionOptimizerParams params =
      OptimizerParamOverrides::resolved<CompositionOptimizerParams>(query.overrides);
  CompositionEmitter emitter{
      items, {}, acceptedEffort, acceptedCost,
      params.effortWeight, params.distanceWeight, config};

  CompositionStrategies::enumerateInsertions(
      query.diff, query.lines,
      [&](const CompositionStrategies::Insertion& s) {
        emitInsertionStrategy(query, s, emitter);
      });

  if (!query.diff.isPureInsertion()) {
    BracketQuoteContext bqContext =
        computeTextObjectContextForDiff(query.diff, query.lines);
    CompositionStrategies::enumerateBracketQuotes(
        query.diff, query.lines, bqContext,
        [&](const CompositionStrategies::BracketQuote& s) {
          emitBracketQuoteStrategy(query, s, emitter);
        });

    optional<JoinPlan> joinPlan =
        computeJoinPlanForDiff(query.diff, query.lines, params, config);
    if (joinPlan) emitJoinAction(query, *joinPlan, emitter);
  }

  sortAndCapSuggestions(items, query.maxCount, query.sortMode);
  return items;
}
