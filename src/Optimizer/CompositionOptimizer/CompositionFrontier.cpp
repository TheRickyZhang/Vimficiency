#include "CompositionFrontier.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "Effort/RunningEffort.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/CompositionOptimizer/CompositionStrategies.h"
#include "Optimizer/CompositionOptimizer/PlannedEditArtifacts.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Types/CharInterval.h"

using namespace std;

namespace {

// Common collector — mirrors TransformFrontier::EditEmitter so the cost
// arithmetic is consistent across the two depth-1 frontiers.
struct CompositionEmitter {
  vector<Suggestion>& items;
  const RunningEffort& acceptedEffort;
  double acceptedCost;
  double effortWeight;
  double distanceWeight;
  const Config& config;

  double costDiffFor(string_view fullSequence) const {
    RunningEffort candidate(globalSequenceToKeys().tokenize(fullSequence), config);
    RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate);
    return merged.getEffort(config) - acceptedCost;
  }

  void emit(string fullSequence, CursorPos landingPos) {
    const double costDiff = costDiffFor(fullSequence);
    Suggestion suggestion{
        .token = Token{std::move(fullSequence)},
        .landingPos = landingPos,
    };
    updateSuggestionMetrics(
        suggestion, costDiff, 0.0,
        effortWeight, distanceWeight);
    items.push_back(std::move(suggestion));
  }
};

// Cheapest depth-1 motion from `cursor` into `[targetLine, beginCol..endCol)`.
// Returns nullopt when no motion lands in the range. Empty string in the
// returned pair means cursor is already in range — emit the structural
// without a motion prefix.
struct MotionPrefix {
  string sequence;       // empty when cursor already in range
  CursorPos landingPos;  // post-motion cursor
};

optional<MotionPrefix> cheapestMotionToRange(
    const Lines& lines, CursorPos cursor,
    int targetLine, int beginCol, int endCol,
    const NavBoundary& boundary, const NavContext& navContext,
    const Config& config) {
  if (targetLine < 0 || beginCol < 0 || endCol <= beginCol) return nullopt;

  const bool inRange = cursor.line == targetLine &&
                       cursor.col >= beginCol && cursor.col < endCol;
  if (inRange) {
    return MotionPrefix{"", cursor};
  }

  CharRange range(CursorPos(targetLine, beginCol), CursorPos(targetLine, endCol));
  CharInterval motionRange(range, lines);

  NavOptimizer navOpt(config);
  auto result = navOpt.optimize(
      lines, cursor, motionRange,
      NavOptimizerParams{}.withMaxResults(1),
      "", boundary, navContext);
  const auto& results = result.getResults();
  if (results.empty() || results[0].getSequence().empty()) return nullopt;
  return MotionPrefix{
      string(results[0].getSequence().view()),
      results[0].getGoalPos(),
  };
}

// Compose a full sequence from optional motion prefix + structural body.
string compose(const MotionPrefix& motion, string_view body) {
  string out = motion.sequence;
  out += body;
  return out;
}

// All three families consume `CompositionStrategies` enumerators that own
// the strategy list — both this file and CompositionOptimizer.cpp call the
// same enumerators, so adding a new strategy in one place flows to both.
// The differing dispatch (frontier-emit vs optimizer-enqueue) lives below.

void emitInsertionStrategy(const CompositionFrontierQuery& query,
                           const CompositionStrategies::Insertion& s,
                           CompositionEmitter& emitter) {
  auto motion = cheapestMotionToRange(
      query.lines, query.cursor, s.targetLine, s.beginCol, s.endCol,
      query.boundary, query.navContext, emitter.config);
  if (!motion) return;
  // Landing for the suggestion is `diff.beginPos` — by the time the user
  // finishes typing the insertCmd, the cursor sits at the insertion point.
  emitter.emit(compose(*motion, s.insertCmd), query.diff.beginPos);
}

void emitBracketQuoteStrategy(const CompositionFrontierQuery& query,
                              const CompositionStrategies::BracketQuote& s,
                              CompositionEmitter& emitter) {
  auto motion = cheapestMotionToRange(
      query.lines, query.cursor, s.line, s.col, s.col + 1,
      query.boundary, query.navContext, emitter.config);
  if (!motion) return;
  emitter.emit(compose(*motion, s.body), query.diff.beginPos);
}

void emitJoinPlan(const CompositionFrontierQuery& query,
                  const JoinPlan& joinPlan,
                  CompositionEmitter& emitter) {
  // joinPlan accepts cursor on the entry line at any column. Compute the
  // cheapest motion to that line; if cursor is already there, motion is empty.
  const Lines& lines = query.lines;
  const int entryLine = joinPlan.entryLine;
  if (entryLine < 0 || entryLine >= static_cast<int>(lines.size())) return;

  auto motion = cheapestMotionToRange(
      lines, query.cursor, entryLine, 0, lines[entryLine].effectiveSize(),
      query.boundary, query.navContext, emitter.config);
  if (!motion) return;
  emitter.emit(compose(*motion, joinPlan.sequence.view()), joinPlan.goalPos);
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
      items, acceptedEffort, acceptedCost,
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
        query.diff, bqContext,
        [&](const CompositionStrategies::BracketQuote& s) {
          emitBracketQuoteStrategy(query, s, emitter);
        });

    optional<JoinPlan> joinPlan =
        computeJoinPlanForDiff(query.diff, query.lines, params, config);
    if (joinPlan) emitJoinPlan(query, *joinPlan, emitter);
  }

  sortAndCapSuggestions(items, query.maxCount, query.sortMode);
  return items;
}
