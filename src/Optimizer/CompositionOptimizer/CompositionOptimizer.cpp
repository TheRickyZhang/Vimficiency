#include "CompositionOptimizer.h"

#include "CompositionSearchContext.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "State/CompositionState.h"
#include "Utils/BracketFlags.h"
#include "Utils/Debug.h"
#include "Utils/QuoteFlags.h"
#include "VimCore/VimCore.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace {

// Compute cursor position after insertion (same for i/a/o/O/I/A + text + Esc)
// Cursor ends on last char of inserted text, or stays at insertPos if empty
Position computeInsertEndPos(Position insertPos, const string& insertedText) {
  assert(!insertedText.empty());
  Lines inserted = Lines::unflatten(insertedText);
  if (inserted.size() == 1) {
    int endCol = insertPos.col + static_cast<int>(inserted[0].size()) - 1;
    return Position(insertPos.line, max(0, endCol));
  } else {
    int lastLine = insertPos.line + static_cast<int>(inserted.size()) - 1;
    int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
    return Position(lastLine, lastCol);
  }
}

} // anonymous namespace

// Note that goalPos doesn't matter except for directionality; we want to explore anything that performs the same edits.
vector<Result> CompositionOptimizer::optimize(
    const Lines& initialLines, const Position initialPos, const Lines& goalLines,
    const Position goalPos, CompositionOptimizerParams params,
    const string& userSequence, const MotionBoundary& boundary,
    const NavContext& navigationContext, const MotionToKeys& rawMotionToKeys) {
  MotionOptimizer motionOptimizer(config);

  // Create search context - handles all pre-computation
  CompositionSearchContext ctx(initialLines, initialPos, goalLines, userSequence,
                               navigationContext, boundary, rawMotionToKeys,
                               params, config);

  if (ctx.totalEdits == 0) {
    return {};
  }
  if (ctx.totalEdits > 16) {
    debug("Cannot support more than 16 edits");
    return {};
  }

  vector<Result> results;

  // Initialize starting state with heuristic cost
  CompositionState startingState(initialPos, Mode::Normal, 0);
  startingState.setCost(ctx.heuristic(startingState, 0));
  ctx.pq.push(startingState);
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  while (ctx.shouldContinue()) {
    CompositionState s = ctx.popNext();
    Position pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    ctx.markProcessed();

    if (ctx.isGoal(s)) {
      results.emplace_back(s.getMotionSequence(),
                           s.getRunningEffort().getEffort(config));
      if (results.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    }

    if (ctx.isStale(s)) {
      ctx.statesSkipped++;
      continue;
    }

    // Get current buffer state
    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
    const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

    // ========== PURE INSERTION HANDLING ==========
    // Pure insertions have no edit region to transition into.
    // We explore navigation + insertion strategies: o/I/A shortcuts or i fallback.
    if (nextEdit.isPureInsertion()) {
      const EditResult& editResult = ctx.editResults[editsCompleted];
      Position insertPos = nextEdit.beginPos;
      bool isNewLineInsertion = nextEdit.isNewLineInsertion();

      // Explore an insertion strategy by navigating to a valid range, then inserting.
      //
      // Each strategy (o/I/A/i) defines a range of cursor positions from which its
      // mode-entry command produces the correct edit. For example, `o` works from
      // any column on the line above, while `i` requires the exact insertion column.
      //
      // The mode-entry command (o/I/A/i) determines the actual insert position
      // independent of where in the range we land, so the final cursor position
      // after typing + Esc is always editResult.goalPos regardless of movement result.
      auto exploreInsertionStrategy = [&](int targetLine, int firstCol, int lastCol,
                                          const string& insertCmd) {
        bool inRange = (pos.line == targetLine &&
                        pos.col >= firstCol && pos.col <= lastCol);

        if (inRange) {
          ctx.exploreEditTransition(s, Sequence(insertCmd), editResult.goalPos,
                                    editsCompleted + 1);
        } else {
          // Slice a padded subset around [pos, target] for MotionOptimizer
          auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
              pos.line, targetLine, params.motionPaddingAbove, params.motionPaddingBelow);

          Lines subset = currentLines.getLineRange(beginLine, endLine);

          // Remap positions to subset-local coordinates
          Position localPos(pos.line - beginLine, pos.col, pos.targetCol);
          Position localRangeFirst(targetLine - beginLine, firstCol);
          Position localRangeLast(targetLine - beginLine, lastCol);

          // Boundary uses full subset extent, not the target range.
          // The target range is only for optimizeToRange's isInRange check.
          // Using the target range as boundary would clamp motions like $ to the range edge.
          Position subsetFirst(0, 0);
          Position subsetLast(static_cast<int>(subset.size()) - 1,
              std::max(0, static_cast<int>(subset.back().size()) - 1));
          MotionBoundary subsetBoundary(subset, subsetFirst, subsetLast,
              beginLine > 0 || boundary.hasLinesAbove(),
              endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

          vector<RangeResult> results = motionOptimizer.optimizeToRange(
              subset, localPos, localRangeFirst, localRangeLast,
              MotionOptimizerRangeParams{}.withMaxResults(1), "",
              subsetBoundary, s.getRunningEffort(),
              navigationContext, ctx.motionToKeys).results;

          for (RangeResult& movResult : results) {
            if (!movResult.isValid()) continue;
            movResult.goalPos.line += beginLine;  // remap back to full-buffer coords

            Sequence fullSeq = movResult.sequence;
            fullSeq.append(insertCmd);
            ctx.exploreEditTransition(s, fullSeq, editResult.goalPos,
                                      editsCompleted + 1);
          }
        }
      };

      // o: skip the trailing newline since the command opens a new line
      if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
        int targetLine = insertPos.line - 1;
        int lastCol = currentLines[targetLine].empty()
            ? 0 : static_cast<int>(currentLines[targetLine].size()) - 1;
        string_view sourceIndent = VimOptions::autoindent()
            ? leadingWhitespace(currentLines[targetLine])
            : string_view{};
        Lines insertLines = Lines::unflatten(string(nextEdit.insertedTextBody()));
        auto [insertText, _] = buildTypedCommands(insertLines, sourceIndent);
        exploreInsertionStrategy(targetLine, 0, lastCol,
                                 "o" + insertText);
      } else {
        // I/A/i: handle autoindent for multi-line insertions
        int fnb = VimCore::firstNonBlankColInLineStr(currentLines[insertPos.line]);
        int lineLen = static_cast<int>(currentLines[insertPos.line].size());
        int lastCol = lineLen == 0 ? 0 : lineLen - 1;
        Lines insertLines = Lines::unflatten(nextEdit.insertedText);

        if (insertPos.col == fnb) {
          // I: insert at first non-blank - navigate anywhere on line
          // For multi-line, prefix before cursor is the indent (text before FNB)
          auto [escaped, _] = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line].substr(0, fnb));
          exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                   "I" + escaped);
        } else if (insertPos.col == lineLen) {
          // A: append at end of line - navigate anywhere on line
          // For multi-line, prefix is the entire current line
          auto [escaped, _] = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line]);
          exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                   "A" + escaped);
        } else {
          // i: fallback - navigate to exact position
          // For multi-line, prefix is text before cursor
          auto [escaped, _] = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line].substr(0, insertPos.col));
          exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col,
                                   "i" + escaped);
        }
      }
      continue;
    }

    // ========== EDIT vs MOVEMENT TRANSITIONS ==========
    const EditResult& editResult = ctx.editResults[editsCompleted];
    auto flatIdx = editResult.flatIndexAt(pos.line, pos.col);

    if (flatIdx.has_value() && editResult.results[*flatIdx].isValid()) {
      ctx.exploreEditTransition(s, editResult.results[*flatIdx].sequence,
                                editResult.goalPos, editsCompleted + 1);
    } else {
      // Check for bracket/quote text object shortcuts
      // These allow reaching the edit region from positions before it on the same line
      const BracketQuoteContext& bqContext = ctx.bracketQuoteContexts[editsCompleted];
      if (bqContext.line == pos.line) {
        const EditResult& editResult = ctx.editResults[editsCompleted];
        const string& insertedText = nextEdit.insertedText;

        if (pos.col < static_cast<int>(bqContext.validQuoteMask.size())) {
          for (char q : QuoteFlags::ALL_QUOTES) {
            if (bqContext.validQuoteMask[pos.col].seen(q)) {
              // Build sequence: c + i/a + quote + insertedText + <Esc>
              string seq = string("c") + bqContext.quoteModifier(q) + q + insertedText + "<Esc>";
              ctx.exploreEditTransition(s, Sequence(seq), editResult.goalPos,
                                        editsCompleted + 1);
            }
          }
        }
        if (pos.col < static_cast<int>(bqContext.validBracketMask.size())) {
          for (char b : BracketFlags::ALL_BRACKETS) {
            if (bqContext.validBracketMask[pos.col].seen(b)) {
              // Build sequence: c + i/a + bracket + insertedText + <Esc>
              string seq = string("c") + bqContext.bracketModifier(b) + b + insertedText + "<Esc>";
              ctx.exploreEditTransition(s, Sequence(seq), editResult.goalPos,
                                        editsCompleted + 1);
            }
          }
        }
      }

      // Slice a padded subset around [pos, edit region] for MotionOptimizer
      Position inclusiveLast = nextEdit.inclusiveLastPos();
      auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
          min(pos.line, nextEdit.beginPos.line),
          max(pos.line, inclusiveLast.line),
          params.motionPaddingAbove, params.motionPaddingBelow);

      Lines subset = currentLines.getLineRange(beginLine, endLine);

      Position localPos(pos.line - beginLine, pos.col, pos.targetCol);
      Position localRangeFirst(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
      Position localRangeLast(inclusiveLast.line - beginLine, inclusiveLast.col);

      // Boundary uses full subset extent, not the edit range.
      // The edit range is only the target for optimizeToRange's isInRange check.
      // Using the edit range as boundary would clamp motions like $ to the range edge.
      Position subsetFirst(0, 0);
      Position subsetLast(static_cast<int>(subset.size()) - 1,
          std::max(0, static_cast<int>(subset.back().size()) - 1));
      MotionBoundary subsetBoundary(subset, subsetFirst, subsetLast,
          beginLine > 0 || boundary.hasLinesAbove(),
          endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

      vector<RangeResult> movementResults = motionOptimizer.optimizeToRange(
          subset, localPos, localRangeFirst, localRangeLast,
          MotionOptimizerRangeParams{}.withMaxResults(
              clamp(nextEdit.origCharCount(), 1, 10)), "",
          subsetBoundary, s.getRunningEffort(),
          navigationContext, ctx.motionToKeys).results;

      for (RangeResult& movResult : movementResults) {
        if (!movResult.isValid()) continue;

        // Remap results back to full-buffer coordinates
        movResult.goalPos.line += beginLine;
        ctx.exploreMotionTransition(s, movResult.sequence, movResult.goalPos,
                                    editsCompleted);
      }
    }
  }

  return results;
}
