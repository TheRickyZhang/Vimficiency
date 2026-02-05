#include "CompositionOptimizer.h"

#include "CompositionSearchContext.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "State/CompositionState.h"
#include "Utils/Debug.h"
#include "Utils/StringUtils.h"
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

// Convert half-open endPos to inclusive lastPos for range-based operations
// With virtual column approach, endPos.line is always correct
Position halfOpenToInclusive(const Position& endPos) {
  return Position(endPos.line, endPos.col - 1);
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

          MotionBoundary subsetBoundary(subset, localRangeFirst, localRangeLast,
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

      // o: new line insertion at col 0 - navigate to line above
      if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
        int targetLine = insertPos.line - 1;
        int lastCol = currentLines[targetLine].empty()
            ? 0 : static_cast<int>(currentLines[targetLine].size()) - 1;
        exploreInsertionStrategy(targetLine, 0, lastCol,
                                 "o" + escapeNewlines(nextEdit.insertedTextBody()) + "<Esc>");
      }

      // I: insertion at first non-blank - navigate anywhere on line
      if (!isNewLineInsertion) {
        int fnb = VimCore::firstNonBlankColInLineStr(currentLines[insertPos.line]);
        if (insertPos.col == fnb) {
          int lastCol = currentLines[insertPos.line].empty()
              ? 0 : static_cast<int>(currentLines[insertPos.line].size()) - 1;
          exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                   "I" + escapeNewlines(nextEdit.insertedText) + "<Esc>");
        }
      }

      // A: insertion at end of line - navigate anywhere on line
      {
        int lineLen = static_cast<int>(currentLines[insertPos.line].size());
        if (insertPos.col == lineLen) {
          int lastCol = lineLen == 0 ? 0 : lineLen - 1;
          if (isNewLineInsertion) {
            // Text ends with newline: use A + content + <CR> + <Esc>
            exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                     "A" + escapeNewlines(nextEdit.insertedTextBody()) + "<CR><Esc>");
          } else {
            exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                     "A" + escapeNewlines(nextEdit.insertedText) + "<Esc>");
          }
        }
      }

      // i: fallback - navigate to exact insertion position
      exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col,
                               "i" + escapeNewlines(nextEdit.insertedText) + "<Esc>");

      continue;  // Skip EDIT TRANSITIONS and MOVEMENT TRANSITIONS
    }

    // ========== EDIT TRANSITIONS ==========
    // Check if we can perform the next edit from current position
    // Uses unified check: flatIndexAt encodes the same position validity as the old bitmask
    // Now handles both regular edits AND pure insertions (which have single-entry EditResult)
    bool editTransitionTaken = false;
    {
      const EditResult& editResult = ctx.editResults[editsCompleted];

      // Convert buffer position to edit region index (O(1) lookup)
      // flatIndexAt returns -1 for out-of-region lines, and we bounds-check for columns
      int flatIdx = editResult.flatIndexAt(pos);
      if (flatIdx >= 0 &&
          flatIdx < static_cast<int>(editResult.results.size())) {
        const Result& editRes = editResult.results[flatIdx];
        if (editRes.isValid()) {
          editTransitionTaken = true;
          // Cursor ends at last char of inserted text (precomputed in editResult.goalPos)
          ctx.exploreEditTransition(s, editRes.sequence, editResult.goalPos,
                                    editsCompleted + 1);
        }
      }
    }

    // ========== MOVEMENT TRANSITIONS ==========
    // Use MotionOptimizer to find optimal paths to next edit region
    if (!editTransitionTaken) {
      // Check for bracket/quote text object shortcuts
      // These allow reaching the edit region from positions before it on the same line
      const TextObjectContext& toCtx = ctx.textObjectContexts[editsCompleted];
      if (toCtx.line == pos.line) {
        const EditResult& editResult = ctx.editResults[editsCompleted];
        const string& insertedText = nextEdit.insertedText;

        // Check valid quotes from this position
        if (pos.col < static_cast<int>(toCtx.validQuoteMask.size())) {
          const QuoteFlags& validQuotes = toCtx.validQuoteMask[pos.col];
          for (char q : {'"', '\'', '`'}) {
            if (validQuotes.seen(q)) {
              // Build sequence: c + i/a + quote + insertedText + <Esc>
              char modifier = toCtx.useAroundQuote.seen(q) ? 'a' : 'i';
              string seq = string("c") + modifier + q + insertedText + "<Esc>";
              ctx.exploreEditTransition(s, Sequence(seq), editResult.goalPos,
                                        editsCompleted + 1);
            }
          }
        }

        // Check valid brackets from this position
        if (pos.col < static_cast<int>(toCtx.validBracketMask.size())) {
          const BracketFlags& validBrackets = toCtx.validBracketMask[pos.col];
          for (char b : {'(', '[', '{', '<'}) {
            if (validBrackets.seen(b)) {
              // Build sequence: c + i/a + bracket + insertedText + <Esc>
              char modifier = toCtx.useAroundBracket.seen(b) ? 'a' : 'i';
              string seq = string("c") + modifier + b + insertedText + "<Esc>";
              ctx.exploreEditTransition(s, Sequence(seq), editResult.goalPos,
                                        editsCompleted + 1);
            }
          }
        }
      }

      // Slice a padded subset around [pos, edit region] for MotionOptimizer.
      // Padding allows overshoot-and-return paths (e.g., j then k to reach a column).
      int regionStart = min(pos.line, nextEdit.beginPos.line);
      int regionEnd = max(pos.line, nextEdit.endPos.line);

      int subsetStart = max(0, regionStart - params.motionPaddingAbove);
      int subsetEnd = min(currentLines.lastLine(), regionEnd + params.motionPaddingBelow);

      Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);  // +1 for exclusive end

      // Remap positions to subset-local coordinates.
      // localRangeFirst/Last = the edit region's target range in subset coords,
      // NOT the subset bounds themselves (beginLine/endLine).
      int lineOffset = subsetStart;
      Position localPos(pos.line - lineOffset, pos.col, pos.targetCol);
      Position localRangeFirst(nextEdit.beginPos.line - lineOffset, nextEdit.beginPos.col);
      Position inclusiveLast = nextEdit.hasDeletedContent()
          ? halfOpenToInclusive(nextEdit.endPos)
          : nextEdit.beginPos;
      Position localRangeLast(inclusiveLast.line - lineOffset, inclusiveLast.col);

      MotionBoundary subsetBoundary(subset, localRangeFirst, localRangeLast,
          subsetStart > 0 || boundary.hasLinesAbove(),
          subsetEnd < currentLines.lastLine() || boundary.hasLinesBelow());

      vector<RangeResult> movementResults = motionOptimizer.optimizeToRange(
          subset, localPos, localRangeFirst, localRangeLast,
          MotionOptimizerRangeParams{}.withMaxResults(
              clamp(nextEdit.origCharCount(), 1, 10)), "",
          subsetBoundary, s.getRunningEffort(),
          navigationContext, ctx.motionToKeys).results;

      // Remap results back to full-buffer coordinates
      for (RangeResult& movResult : movementResults) {
        movResult.goalPos.line += lineOffset;
      }

      for (const RangeResult& movResult : movementResults) {
        if (!movResult.isValid())
          continue;

        ctx.exploreMotionTransition(s, movResult.sequence, movResult.goalPos,
                                    editsCompleted);
      }
    }
  }

  return results;
}
