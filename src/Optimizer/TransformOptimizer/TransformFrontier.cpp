#include "TransformFrontier.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

#include "Effort/RunningEffort.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/CompositionOptimizer/CompositionStepArtifacts.h"
#include "Optimizer/TransformOptimizer/TransformSequenceDecomposition.h"
#include "Types/BracketFlags.h"
#include "Types/QuoteFlags.h"
#include "VimCore/VimCore.h"

using namespace std;

namespace {

TransformFrontierItem frontierItemFromSequence(
    string_view fullSequence,
    double cost,
    CursorPos goalPos) {
  TransformSequenceDecomposition decomposition = decomposeEditSequence(fullSequence);
  return TransformFrontierItem{
      FrontierItem{
          .molecule = std::move(decomposition.molecule),
          .goalPos = goalPos,
          .cost = cost,
      },
      string(fullSequence),                    // fullSequence
      std::move(decomposition.typedText),      // typedText
  };
}

bool cursorInInsertionRange(
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol) {
  return cursor.line == targetLine && cursor.col >= beginCol && cursor.col < endCol;
}

// Emission context — shared by every push site in rankTransformFrontier so dedup
// lives at the generation boundary (not a post-hoc filter).
//
// Dedup key for edits is the full sequence text, NOT the landing position.
// Rationale: for a narrow diff (e.g. 1-char rename `n`→`m`), every valid
// strategy — `rm`, `sm<Esc>`, `cl m<Esc>`, ... — lands at the same
// post-edit cursor cell. Landing-based dedup would collapse them all to
// the cheapest and hide the pedagogical alternatives, which are the whole
// point of the explore frontier. Motions use landing-based dedup because
// there the cell IS the outcome; edits dedup by command shape because
// the command shape IS the outcome we're teaching.
struct EditEmitter {
  vector<TransformFrontierItem>& items;
  unordered_set<string> seenSequence;
  int maxCount;
  bool allowMultiplePerPosition;

  // Try to emit. Returns true iff caller should continue emitting more
  // items; returns false when we've hit maxCount (caller should return).
  bool emit(TransformFrontierItem item) {
    if (!allowMultiplePerPosition) {
      if (!seenSequence.insert(item.fullSequence).second) {
        return static_cast<int>(items.size()) < maxCount;
      }
    }
    items.push_back(std::move(item));
    return static_cast<int>(items.size()) < maxCount;
  }
};

bool appendInsertionStrategy(
    EditEmitter& emitter,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    string fullSequence,
    CursorPos goalPos,
    const Config& config) {
  if (targetLine < 0) return true;
  if (beginCol < 0 || endCol <= beginCol) return true;
  if (!cursorInInsertionRange(cursor, targetLine, beginCol, endCol)) return true;
  return emitter.emit(frontierItemFromSequence(
      fullSequence, getEffort(fullSequence, config), goalPos));
}

}

vector<TransformFrontierItem> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config) {
  if (query.maxCount <= 0) return {};

  CompositionOptimizerParams params;
  const int totalStarts = query.diff.isPureInsertion()
      ? 1
      : max(1, query.diff.deletedLines().totalPositions());
  params.withMaxResults(totalStarts * query.maxCount);
  params.withMaxTransformResultsPerPosition(query.maxCount);
  TransformResult transformResult = computeTransformResultForDiff(query.diff, params, config);
  optional<JoinPlan> joinPlan = computeJoinPlanForDiff(query.diff, query.lines, params, config);
  BracketQuoteContext bqContext = computeTextObjectContextForDiff(query.diff, query.lines);

  vector<TransformFrontierItem> items;
  items.reserve(static_cast<size_t>(query.maxCount));
  EditEmitter emitter{items, {}, query.maxCount, query.allowMultiplePerPosition};

  span<const Result> starts = transformResult.resultsAt(query.cursor.line, query.cursor.col);
  if (!starts.empty()) {
    const CursorPos goalPos = transformResult.goalPosAt(query.cursor.line, query.cursor.col);
    for (const Result& result : starts) {
      if (!result.isValid() || result.getSequence().empty()) continue;
      if (!emitter.emit(frontierItemFromSequence(
              result.getSequence().view(), result.getCost(), goalPos))) {
        return items;
      }
    }
  }

  const CursorPos stepGoalPos = transformResult.getGoalPos();

  if (query.diff.isPureInsertion()) {
    const CursorPos insertPos = query.diff.beginPos;
    const bool isNewLineInsertion = query.diff.isNewLineInsertion();

    if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
      const int targetLine = insertPos.line - 1;
      const int lineEnd = query.lines[targetLine].effectiveSize();
      string_view sourceIndent = VimOptions::autoindent()
          ? leadingWhitespace(query.lines[targetLine])
          : string_view{};
      Lines insertLines = Lines::unflatten(string(query.diff.insertedTextBody()));
      KeyedSequence typed = buildTypedCommands(insertLines, sourceIndent);
      if (!appendInsertionStrategy(emitter, query.cursor, targetLine, 0, lineEnd,
                                   "o" + typed.seq.str(), stepGoalPos, config))
        return items;
    } else {
      const int fnb = VimCore::firstNonBlankColInLineStr(query.lines[insertPos.line]);
      const int lineLen = static_cast<int>(query.lines[insertPos.line].size());
      const int lineEnd = query.lines[insertPos.line].effectiveSize();
      const int lastContentCol = lineEnd - 1;
      Lines insertLines = Lines::unflatten(query.diff.insertedText);

      if (insertPos.col == fnb) {
        KeyedSequence escaped = buildTypedCommands(
            insertLines, "", query.lines[insertPos.line].substr(0, fnb));
        if (!appendInsertionStrategy(emitter, query.cursor, insertPos.line, 0, lineEnd,
                                     "I" + escaped.seq.str(), stepGoalPos, config))
          return items;
        if (!appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                     insertPos.col, insertPos.col + 1,
                                     "i" + escaped.seq.str(), stepGoalPos, config))
          return items;
      } else if (insertPos.col == lineLen) {
        KeyedSequence escaped = buildTypedCommands(
            insertLines, "", query.lines[insertPos.line]);
        if (!appendInsertionStrategy(emitter, query.cursor, insertPos.line, 0, lineEnd,
                                     "A" + escaped.seq.str(), stepGoalPos, config))
          return items;
        if (!appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                     lastContentCol, lastContentCol + 1,
                                     "a" + escaped.seq.str(), stepGoalPos, config))
          return items;
      } else {
        KeyedSequence escaped = buildTypedCommands(
            insertLines, "", query.lines[insertPos.line].substr(0, insertPos.col));
        if (!appendInsertionStrategy(emitter, query.cursor, insertPos.line,
                                     insertPos.col, insertPos.col + 1,
                                     "i" + escaped.seq.str(), stepGoalPos, config))
          return items;
      }
    }
  }

  if (joinPlan && query.cursor.line == joinPlan->entryLine) {
    if (!emitter.emit(frontierItemFromSequence(
            joinPlan->sequence.view(), joinPlan->effort, joinPlan->goalPos)))
      return items;
  }

  if (!starts.empty()) return items;

  if (bqContext.line != query.cursor.line) return items;

  const string& insertedText = query.diff.insertedText;
  const bool pureDeletion = query.diff.isPureDeletion();
  const char textObjOp = pureDeletion ? 'd' : 'c';

  if (query.cursor.col < static_cast<int>(bqContext.validQuoteMask.size())) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[query.cursor.col].seen(q)) continue;
      string seq = string(1, textObjOp) + bqContext.quoteModifier(q) + q;
      if (!pureDeletion) {
        seq += insertedText;
        seq += "<Esc>";
      }
      if (!emitter.emit(frontierItemFromSequence(
              seq, getEffort(seq, config), stepGoalPos)))
        return items;
    }
  }

  if (query.cursor.col < static_cast<int>(bqContext.validBracketMask.size())) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[query.cursor.col].seen(b)) continue;
      string seq = string(1, textObjOp) + bqContext.bracketModifier(b) + b;
      if (!pureDeletion) {
        seq += insertedText;
        seq += "<Esc>";
      }
      if (!emitter.emit(frontierItemFromSequence(
              seq, getEffort(seq, config), stepGoalPos)))
        return items;
    }
  }

  return items;
}
