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
#include "Optimizer/TransformOptimizer/ChangeGoalHandler.h"
#include "Optimizer/TransformOptimizer/TransformExplorer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
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
  vector<FrontierItem>& items;
#ifndef NDEBUG
  unordered_set<string> seenSequence_debug;
#endif
  const RunningEffort& acceptedEffort;
  double acceptedCost;
  const Config& config;

  double costDiffFor(string_view fullSequence) const {
    RunningEffort candidate(globalSequenceToKeys().tokenize(fullSequence), config);
    RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate);
    return merged.getEffort(config) - acceptedCost;
  }

  void emit(string_view fullSequence, CursorPos landingPos) {
#ifndef NDEBUG
    assert(seenSequence_debug.insert(string(fullSequence)).second &&
           "duplicate sequence reached EditEmitter::emit — enumerator bug");
#endif
    items.push_back(FrontierItem{
        .token = extractStructuralToken(fullSequence),
        .landingPos = landingPos,
        .costDiff = costDiffFor(fullSequence),
    });
  }
};

void appendInsertionStrategy(
    EditEmitter& emitter,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    string fullSequence,
    CursorPos landingPos) {
  if (targetLine < 0) return;
  if (beginCol < 0 || endCol <= beginCol) return;
  if (!cursorInInsertionRange(cursor, targetLine, beginCol, endCol)) return;
  emitter.emit(fullSequence, landingPos);
}

// Depth-1 structural enumeration via TransformExplorer — replaces the
// optimizer-driven path for diffs with deleted content (pure deletion +
// replacement). Pure insertion is still handled by inline emission below.
//
// MIRROR: setup mirrors `TransformOptimizer::optimizeImpl` (effective lines =
// prefix + deletedLines + suffix; cursor mapped to local edit-region coords).
// Validity check: applying the structural via TransformState's after*()
// methods must produce the post-deletion effective lines (prefix + suffix as
// one line). Replacement diffs convert deletions to changes via asChange().
void enumerateDepth1DeletionStructurals(
    const TransformFrontierQuery& query,
    const Config& config,
    EditEmitter& emitter) {
  const DiffState& diff = query.diff;
  if (!diff.hasDeletedContent()) return;

  Lines effective = diff.deletedLines();
  if (effective.empty()) return;
  const auto& boundary = diff.boundary;
  if (!boundary.prefix().empty()) {
    effective.front().insert(0, boundary.prefix());
  }
  if (!boundary.suffix().empty()) {
    effective.back() += boundary.suffix();
  }

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
  TransformState state(effective, localCursor, /*startIndex=*/0, /*initialCost=*/0.0);

  const bool isReplacement = diff.isReplacement();

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

  auto applyAndEmitCharwise = [&](TransformState&& newState, const SequenceBinding& cmd) {
    if (newState.getLines() != expectedPost) return;
    emitter.emit(charwiseSeq(cmd), bufferLanding(newState.getPos()));
  };

  auto applyAndEmitLinewise = [&](TransformState&& newState, const SequenceBinding& cmd,
                                    std::string_view lineContent) {
    if (newState.getLines() != expectedPost) return;
    emitter.emit(linewiseSeq(cmd, lineContent), bufferLanding(newState.getPos()));
  };

  // Polymorphic over CharRange / CharLineRange / LineCharRange — TransformExplorer
  // calls the same callback for all three with different range types.
  auto onAnyDeletion = [&](auto&& range, const SequenceBinding& cmd) {
    using R = std::decay_t<decltype(range)>;
    if constexpr (std::is_same_v<R, CharRange>) {
      applyAndEmitCharwise(state.afterDeletion(range), cmd);
    } else if constexpr (std::is_same_v<R, CharLineRange>) {
      applyAndEmitCharwise(state.afterCharLineDeletion(range), cmd);
    } else if constexpr (std::is_same_v<R, LineCharRange>) {
      applyAndEmitCharwise(state.afterLineCharDeletion(range), cmd);
    }
  };

  auto onLinewise = [&](const LineRange& range, const SequenceBinding& cmd) {
    // For change-form conversion, deleteToChangeLine looks at the first
    // deleted line's content to decide between `cc` and `0C`.
    std::string_view firstLineContent =
        (range.beginLine >= 0 && range.beginLine < static_cast<int>(effective.size()))
            ? std::string_view(effective[range.beginLine])
            : std::string_view{};
    applyAndEmitLinewise(state.afterMultiLinewiseDeletion(range, boundary.hasLinesBelow()),
                          cmd, firstLineContent);
  };

  // Joins are surfaced via the separate joinPlan path in rankTransformFrontier;
  // skip emission here to avoid double-counting and to keep this helper
  // deletion-focused.
  auto onJoin = [&](bool /*addSpace*/, const SequenceBinding& /*cmd*/) {};

  explorer.exploreAllDeletions(state, onAnyDeletion, onLinewise, onJoin);
  explorer.exploreCountedLineEdits(localCursor, effective, params.minPrefixCount, onLinewise);
  explorer.exploreCountedWordEdits(localCursor, effective, params.minPrefixCount, onAnyDeletion);

  // Counted char edits: bound by the line's content (excluding prefix/suffix).
  const int contentStart = (localCursor.line == 0) ? boundary.leftColOffset() : 0;
  int contentEnd = static_cast<int>(effective[localCursor.line].size());
  if (localCursor.line == static_cast<int>(effective.size()) - 1) {
    contentEnd -= boundary.rightColOffset();
  }
  if (contentEnd > contentStart) {
    explorer.exploreCountedCharEdits(localCursor, effective, contentStart, contentEnd,
                                     params.minPrefixCount, onAnyDeletion);
  }
}

}

vector<FrontierItem> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config) {
  if (query.maxCount <= 0) return {};

  CompositionOptimizerParams params;
  optional<JoinPlan> joinPlan = computeJoinPlanForDiff(query.diff, query.lines, params, config);
  BracketQuoteContext bqContext = computeTextObjectContextForDiff(query.diff, query.lines);

  vector<FrontierItem> items;
  items.reserve(static_cast<size_t>(query.maxCount));
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.acceptedSeq), config);
  const double acceptedCost = acceptedEffort.getEffort(config);
  EditEmitter emitter{
      items,
#ifndef NDEBUG
      {},
#endif
      acceptedEffort, acceptedCost, config};
  auto finish = [&]() -> vector<FrontierItem> {
    stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
      return a.costDiff < b.costDiff;
    });
    if (items.size() > static_cast<size_t>(query.maxCount))
      items.resize(static_cast<size_t>(query.maxCount));
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

    if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
      const int targetLine = insertPos.line - 1;
      const int lineEnd = query.lines[targetLine].effectiveSize();
      appendInsertionStrategy(emitter, query.cursor, targetLine, 0, lineEnd,
                              "o", editGoalPos);
    } else {
      const int fnb = VimCore::firstNonBlankColInLineStr(query.lines[insertPos.line]);
      const int lineLen = static_cast<int>(query.lines[insertPos.line].size());
      const int lineEnd = query.lines[insertPos.line].effectiveSize();
      const int lastContentCol = lineEnd - 1;

      if (insertPos.col == fnb) {
        appendInsertionStrategy(emitter, query.cursor, insertPos.line, 0, lineEnd,
                                "I", editGoalPos);
        appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                insertPos.col, insertPos.col + 1,
                                "i", editGoalPos);
      } else if (insertPos.col == lineLen) {
        appendInsertionStrategy(emitter, query.cursor, insertPos.line, 0, lineEnd,
                                "A", editGoalPos);
        appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                lastContentCol, lastContentCol + 1,
                                "a", editGoalPos);
      } else {
        appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                insertPos.col, insertPos.col + 1,
                                "i", editGoalPos);
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

  if (query.cursor.col < static_cast<int>(bqContext.validQuoteMask.size())) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[query.cursor.col].seen(q)) continue;
      string seq = string(1, textObjOp) + bqContext.quoteModifier(q) + q;
      emitter.emit(seq, editGoalPos);
    }
  }

  if (query.cursor.col < static_cast<int>(bqContext.validBracketMask.size())) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[query.cursor.col].seen(b)) continue;
      string seq = string(1, textObjOp) + bqContext.bracketModifier(b) + b;
      emitter.emit(seq, editGoalPos);
    }
  }

  return finish();
}
