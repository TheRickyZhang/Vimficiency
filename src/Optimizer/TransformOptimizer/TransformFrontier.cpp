#include "TransformFrontier.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "Effort/EffortBank.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/CompositionOptimizer/PlannedEditArtifacts.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/TransformOptimizer/ChangeGoalHandler.h"
#include "Optimizer/TransformOptimizer/TransformExplorer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformPostExplorerEmissions.h"
#include "Optimizer/TransformOptimizer/TransformSequenceDecomposition.h"
#include "Optimizer/TransformOptimizer/TransformState.h"
#include "Types/BracketFlags.h"
#include "Types/QuoteFlags.h"
#include "VimCore/VimCore.h"

using namespace std;

namespace {

bool cursorInInsertionRange(
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol) {
  return cursor.line == targetLine && cursor.col >= beginCol && cursor.col < endCol;
}

// Emission context — shared by every push site in rankTransformFrontier.
//
// After the depth-1 refactor (Phase A.1) the emission sources are
// structurally distinct: each enumerator (TransformExplorer-driven
// deletions, pure-insertion mode-entry commands, joinPlan, bracket/quote
// text-objects) produces its own command-shape lane and they don't
// overlap. So no runtime dedup is needed; if a duplicate sequence ever
// reaches `emit`, that's a bug in one of the enumerators — caught by the
// debug assert below.
struct EditEmitter {
  vector<Suggestion>& items;
#ifndef NDEBUG
  unordered_set<string> seenSequence_debug;
#endif
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

  void emit(string_view fullSequence, CursorPos landingPos, double distance = 0.0) {
#ifndef NDEBUG
    assert(seenSequence_debug.insert(string(fullSequence)).second &&
           "duplicate sequence reached EditEmitter::emit — enumerator bug");
#endif
    Suggestion suggestion{
        .token = extractStructuralToken(fullSequence),
        .landingPos = landingPos,
    };
    updateSuggestionMetrics(
        suggestion, costDiffFor(fullSequence), distance,
        effortWeight, distanceWeight);
    items.push_back(std::move(suggestion));
  }
};

void appendInsertionStrategy(
    EditEmitter& emitter,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    string fullSequence,
    CursorPos landingPos,
    double distance) {
  if (targetLine < 0) return;
  if (beginCol < 0 || endCol <= beginCol) return;
  if (!cursorInInsertionRange(cursor, targetLine, beginCol, endCol)) return;
  emitter.emit(fullSequence, landingPos, distance);
}

// Depth-1 structural enumeration via TransformExplorer — replaces the
// optimizer-driven path for diffs with deleted content (pure deletion +
// replacement). Pure insertion is still handled by inline emission below.
//
// Effective-lines wrapping and cursor mapping flow through
// `TransformBoundary::withBoundary` and the local-cursor translation right
// below — same primitives the optimizer's main loop uses, so the two paths
// can't drift on the boundary semantics.
void enumerateDepth1DeletionStructurals(
    const TransformFrontierQuery& query,
    const Config& config,
    EditEmitter& emitter) {
  const DiffState& diff = query.diff;
  if (!diff.hasDeletedContent()) return;

  Lines deleted = diff.deletedLines();
  if (deleted.empty()) return;
  const auto& boundary = diff.boundary;
  Lines effective = boundary.withBoundary(std::move(deleted));

  // After fully deleting the edit region, effective collapses to one line
  // containing prefix + suffix.
  Lines expectedPost;
  expectedPost.push_back(Line(boundary.prefix() + boundary.suffix()));

  // Map buffer cursor to effective-line local coords. effCol == bufferCol
  // for line 0 (because leftColOffset == prefix.length()) and for line N>0
  // (no shift). See TransformOptimizer::optimizeImpl's setup loop for the
  // canonical translation.
  CursorPos localCursor(query.cursor.line - diff.beginPos.line, query.cursor.col);
  if (localCursor.line < 0 || localCursor.line >= static_cast<int>(effective.size())) return;
  const int lineSize = static_cast<int>(effective[localCursor.line].size());
  if (localCursor.col < 0 || localCursor.col > lineSize) return;

  TransformOptimizerParams params;
  EffortBank bank(config);
  TransformExplorer explorer(boundary, params, config, bank,
                              boundary.leftColOffset(),
                              boundary.rightColOffset());
  TransformEditorState state(effective, localCursor);

  const bool isReplacement = diff.isReplacement();
  const double replacementInsertDistance = isReplacement
      ? textDistanceEstimate(diff.insertedLines())
      : 0.0;

  // Translate effective-line cursor back to buffer coords.
  auto bufferLanding = [&](CursorPos local) -> CursorPos {
    return CursorPos(local.line + diff.beginPos.line, local.col);
  };

  // Build the displayed sequence string for an emitted command. For
  // replacement diffs, deletion commands convert to change-mode form via
  // ChangeGoalHandler::deleteToChangeChar / deleteToChangeLine — same
  // mapping the optimizer's change-goal handler uses (covers `D`→`C`,
  // `x`→`s`, `X`→`hs`, `dw`→`dwi`, `dd`→`cc`/`0C`, etc.).
  auto charwiseSeq = [&](const SequenceBinding& cmd) -> string {
    if (!isReplacement) {
      string seq;
      if (cmd.count > 0) seq += to_string(cmd.count);
      seq += string(cmd.base.seq.view());
      return seq;
    }
    KeyedSequence c = ChangeGoalHandler::deleteToChangeChar(cmd);
    return string(c.seq.view());
  };

  auto linewiseSeq = [&](const SequenceBinding& cmd, std::string_view lineContent) -> string {
    if (!isReplacement) {
      string seq;
      if (cmd.count > 0) seq += to_string(cmd.count);
      seq += string(cmd.base.seq.view());
      return seq;
    }
    KeyedSequence c = ChangeGoalHandler::deleteToChangeLine(cmd, lineContent);
    return string(c.seq.view());
  };

  auto applyAndEmitCharwise = [&](TransformEditorState&& newState, const SequenceBinding& cmd) {
    if (newState.getLines() != expectedPost) return;
    emitter.emit(charwiseSeq(cmd), bufferLanding(newState.getPos()),
                 replacementInsertDistance);
  };

  auto applyAndEmitLinewise = [&](TransformEditorState&& newState, const SequenceBinding& cmd,
                                    std::string_view lineContent) {
    if (newState.getLines() != expectedPost) return;
    emitter.emit(linewiseSeq(cmd, lineContent), bufferLanding(newState.getPos()),
                 replacementInsertDistance);
  };

  // Polymorphic over CharRange / CharLineRange / LineCharRange — TransformExplorer
  // calls the same callback for all three with different range types.
  auto onAnyDeletion = [&](auto&& range, const SequenceBinding& cmd) {
    using R = std::decay_t<decltype(range)>;
    if constexpr (std::is_same_v<R, CharRange>) {
      applyAndEmitCharwise(TransformSimulator::afterDeletion(state, range), cmd);
    } else if constexpr (std::is_same_v<R, CharLineRange>) {
      applyAndEmitCharwise(TransformSimulator::afterCharLineDeletion(state, range), cmd);
    } else if constexpr (std::is_same_v<R, LineCharRange>) {
      applyAndEmitCharwise(TransformSimulator::afterLineCharDeletion(state, range), cmd);
    }
  };

  auto onLinewise = [&](const LineRange& range, const SequenceBinding& cmd) {
    // For change-form conversion, deleteToChangeLine looks at the first
    // deleted line's content to decide between `cc` and `0C`.
    std::string_view firstLineContent =
        (range.beginLine >= 0 && range.beginLine < static_cast<int>(effective.size()))
            ? std::string_view(effective[range.beginLine])
            : std::string_view{};
    applyAndEmitLinewise(
        afterLinewiseDeletionForCommand(
            state, range, boundary.hasLinesBelow(), cmd.base.seq.view()),
        cmd, firstLineContent);
  };

  // Pure deletion of `\n` is the only depth-1 case where a bare J/gJ/NJ/NgJ
  // matches the post-edit fencepost. Replacement-with-join (e.g. `\n` → ` `)
  // is covered by the separate joinPlan emission downstream, which builds
  // the correct `J`/`Ji<typed>` shape — skipping here avoids both
  // duplication and incomplete change-mode reconstruction.
  const bool emitJoinStructurals = diff.isPureDeletion();
  auto onJoin = [&](bool addSpace, const SequenceBinding& cmd) {
    if (!emitJoinStructurals) return;
    applyAndEmitCharwise(TransformSimulator::afterJoin(state, addSpace), cmd);
  };
  auto onCountedJoin = [&](bool addSpace, const SequenceBinding& cmd) {
    if (!emitJoinStructurals) return;
    applyAndEmitCharwise(TransformSimulator::afterMultiJoin(state, cmd.count, addSpace), cmd);
  };

  // MIRROR: identical sweep to TransformOptimizer::optimizeImpl's main loop —
  // see the source-of-truth comment on sweepExplorerStructurals in
  // TransformExplorer.h. Adding a new explorer enumeration there flows here
  // automatically.
  sweepExplorerStructurals(
      explorer, state, effective, localCursor,
      boundary.leftColOffset(), boundary.rightColOffset(),
      params.minPrefixCount,
      onAnyDeletion, onLinewise, onJoin, onLinewise, onCountedJoin);

  // Post-explorer emissions — see TransformPostExplorerEmissions.h for the
  // full list. Each entry mirrors a corresponding finalize-time emission
  // in the optimizer's GoalHandlers.
  if (diff.isPureDeletion()) {
    auto visual = TransformPostExplorer::tryVisualDelete(
        effective, boundary.leftColOffset(), boundary.rightColOffset(),
        boundary, params, config);
    if (visual) {
      emitter.emit(visual->getSequence().view(),
                   bufferLanding(localCursor));
    }
  }
  if (diff.isReplacement() && diff.deletedLines().size() == 1 &&
      diff.insertedLines().size() == 1 &&
      diff.deletedLines()[0].size() == diff.insertedLines()[0].size()) {
    // tryReplacement emits a sequence assuming the cursor is at col 0 of the
    // single line. Mirror the finalize-time gate in ChangeGoalHandler:
    // emit only when our depth-1 cursor is at the start of the diff line.
    if (localCursor.line == 0 && localCursor.col == boundary.leftColOffset()) {
      auto replacement = ChangeGoalHandler::tryReplacement(
          diff.deletedLines()[0], diff.insertedLines()[0],
          config, std::numeric_limits<double>::infinity());
      if (replacement) {
        emitter.emit(replacement->getSequence().view(), bufferLanding(localCursor));
      }
    }
  }
}

}

vector<Suggestion> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config) {
  if (query.maxCount <= 0) return {};

  CompositionOptimizerParams compositionParams =
      OptimizerParamOverrides::resolved<CompositionOptimizerParams>(query.overrides);
  TransformOptimizerParams transformParams =
      OptimizerParamOverrides::resolved<TransformOptimizerParams>(query.overrides);
  optional<JoinPlan> joinPlan = computeJoinPlanForDiff(
      query.diff, query.lines, compositionParams, config);
  BracketQuoteContext bqContext = computeTextObjectContextForDiff(query.diff, query.lines);

  vector<Suggestion> items;
  items.reserve(static_cast<size_t>(query.maxCount));
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort(config);
  EditEmitter emitter{
      items,
#ifndef NDEBUG
      {},
#endif
      acceptedEffort, acceptedCost,
      transformParams.effortWeight, transformParams.distanceWeight,
      config};
  auto finish = [&]() -> vector<Suggestion> {
    sortAndCapSuggestions(items, query.maxCount, query.sortMode);
    return std::move(items);
  };

  // Depth-1 deletion / replacement enumeration via TransformExplorer.
  // Replaces the prior `computeTransformResultForDiff` call which ran a
  // full A* over multi-token sequences. Pure insertion is still handled by
  // the inline branches further down.
  const size_t itemsBeforeDeletionEnum = items.size();
  enumerateDepth1DeletionStructurals(query, config, emitter);
  const bool gotDeletionStructurals = items.size() > itemsBeforeDeletionEnum;

  // Post-edit cursor target for inline emission paths (pure insertion +
  // bracket/quote text-objects). Previously sourced from
  // `transformResult.getGoalPos()`; for depth-1 the equivalent is the
  // diff's beginPos (cursor lands at the deletion/insertion start after
  // the structural completes — the typed content is owned by Insert
  // phase).
  const CursorPos editGoalPos = query.diff.beginPos;

  // Pure-insertion structurals: emit the bare mode-entry command (`o`,
  // `O`, `i`, `I`, `a`, `A`). The typed content + <Esc> are owned by the
  // Insert phase recommendation; the depth-1 transform rec only describes
  // the structural decision the user makes from normal mode.
  if (query.diff.isPureInsertion()) {
    const CursorPos insertPos = query.diff.beginPos;
    const bool isNewLineInsertion = query.diff.isNewLineInsertion();
    const double insertDistance = textDistanceEstimate(query.diff.insertedLines());

    if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
      const int targetLine = insertPos.line - 1;
      const int lineEnd = query.lines[targetLine].effectiveSize();
      appendInsertionStrategy(emitter, query.cursor, targetLine, 0, lineEnd,
                              "o", editGoalPos, insertDistance);
    } else {
      const int fnb = VimCore::firstNonBlankColInLineStr(query.lines[insertPos.line]);
      const int lineLen = static_cast<int>(query.lines[insertPos.line].size());
      const int lineEnd = query.lines[insertPos.line].effectiveSize();
      const int lastContentCol = lineEnd - 1;

      if (insertPos.col == fnb) {
        appendInsertionStrategy(emitter, query.cursor, insertPos.line, 0, lineEnd,
                                "I", editGoalPos, insertDistance);
        appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                insertPos.col, insertPos.col + 1,
                                "i", editGoalPos, insertDistance);
      } else if (insertPos.col == lineLen) {
        appendInsertionStrategy(emitter, query.cursor, insertPos.line, 0, lineEnd,
                                "A", editGoalPos, insertDistance);
        appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                lastContentCol, lastContentCol + 1,
                                "a", editGoalPos, insertDistance);
      } else {
        appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                insertPos.col, insertPos.col + 1,
                                "i", editGoalPos, insertDistance);
      }
    }
  }

  if (joinPlan && query.cursor.line == joinPlan->entryLine) {
    emitter.emit(joinPlan->sequence.view(), joinPlan->goalPos);
  }

  // Bracket/quote text-objects only fire as a fallback when no deletion
  // structurals were found — preserves the prior behaviour that the
  // optimizer's results path gated.
  if (gotDeletionStructurals) return finish();

  if (bqContext.line != query.cursor.line) return finish();

  // Bracket/quote text-objects: emit the bare structural command. For
  // pure-deletion diffs the structural is the full edit (`di"`, `da{`).
  // For replacement, the structural is the change-mode form (`ci"`,
  // `ca{`); the typed replacement content lives in the Insert phase rec.
  const bool pureDeletion = query.diff.isPureDeletion();
  const char textObjOp = pureDeletion ? 'd' : 'c';
  const double textObjectDistance = pureDeletion
      ? 0.0
      : textDistanceEstimate(query.diff.insertedLines());

  if (query.cursor.col < static_cast<int>(bqContext.validQuoteMask.size())) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[query.cursor.col].seen(q)) continue;
      string seq = string(1, textObjOp) + bqContext.quoteModifier(q) + q;
      emitter.emit(seq, editGoalPos, textObjectDistance);
    }
  }

  if (query.cursor.col < static_cast<int>(bqContext.validBracketMask.size())) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[query.cursor.col].seen(b)) continue;
      string seq = string(1, textObjOp) + bqContext.bracketModifier(b) + b;
      emitter.emit(seq, editGoalPos, textObjectDistance);
    }
  }

  return finish();
}
