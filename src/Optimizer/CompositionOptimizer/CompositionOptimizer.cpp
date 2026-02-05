#include "CompositionOptimizer.h"

#include "CompositionSearchContext.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "State/CompositionState.h"
#include "Utils/Debug.h"
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
    const Position goalPos, const string& userSequence,
    const NavContext& navigationContext, const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys, CompositionOptimizerParams params) {
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
      const string& text = nextEdit.insertedText;

      bool isNewLineInsertion = !text.empty() && text.back() == '\n';
      string textContent = isNewLineInsertion
          ? text.substr(0, text.size() - 1)
          : text;

      // Escape newlines in text for use in Vim sequences (replace \n with <CR>)
      auto escapeNewlines = [](const string& s) {
        string result;
        result.reserve(s.size());
        for (char c : s) {
          if (c == '\n') {
            result += "<CR>";
          } else {
            result += c;
          }
        }
        return result;
      };

      // Helper to explore an insertion strategy: navigate to line range, then insert
      auto exploreInsertionStrategy = [&](int targetLine, int firstCol, int lastCol,
                                          const string& insertCmd) {
        bool inRange = (pos.line == targetLine &&
                        pos.col >= firstCol && pos.col <= lastCol);

        if (inRange) {
          // Already in valid range - use insert command directly
          ctx.exploreEditTransition(s, Sequence(insertCmd), editResult.goalPos,
                                    editsCompleted + 1);
        } else {
          // Navigate to range, then use insert command
          int regionStart = min(pos.line, targetLine);
          int regionEnd = max(pos.line, targetLine);
          int subsetStart = max(0, regionStart - params.motionLinePaddingAbove);
          int subsetEnd = min(currentLines.lastLine(),
                              regionEnd + params.motionLinePaddingBelow);

          Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);
          int lineOffset = subsetStart;

          Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);
          Position subsetFirst(targetLine - lineOffset, firstCol);
          Position subsetLast(targetLine - lineOffset, lastCol);

          MotionBoundary subsetBoundary(subset, subsetFirst, subsetLast,
              subsetStart > 0 || boundary.hasLinesAbove(),
              subsetEnd < currentLines.lastLine() || boundary.hasLinesBelow());

          vector<RangeResult> results = motionOptimizer.optimizeToRange(
              subset, subsetPos, s.getRunningEffort(), subsetFirst, subsetLast,
              "", navigationContext, subsetBoundary, ctx.motionToKeys,
              MotionOptimizerRangeParams{}.withMaxResults(1)).results;

          for (RangeResult& movResult : results) {
            if (!movResult.isValid()) continue;
            movResult.goalPos.line += lineOffset;

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
                                 "o" + escapeNewlines(textContent) + "<Esc>");
      }

      // I: insertion at first non-blank - navigate anywhere on line
      if (!isNewLineInsertion) {
        int fnb = VimCore::firstNonBlankColInLineStr(currentLines[insertPos.line]);
        if (insertPos.col == fnb) {
          int lastCol = currentLines[insertPos.line].empty()
              ? 0 : static_cast<int>(currentLines[insertPos.line].size()) - 1;
          exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                   "I" + escapeNewlines(text) + "<Esc>");
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
                                     "A" + escapeNewlines(textContent) + "<CR><Esc>");
          } else {
            exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                     "A" + escapeNewlines(text) + "<Esc>");
          }
        }
      }

      // i: fallback - navigate to exact insertion position
      // Use escapeNewlines to convert \n to <CR> for valid Vim syntax
      exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col,
                               "i" + escapeNewlines(text) + "<Esc>");

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

      // Calculate line subset bounds for MotionOptimizer
      // Include padding to allow overshoot-and-return paths
      // With half-open semantics, endPos.line is always the correct last affected line
      int regionStart = min(pos.line, nextEdit.beginPos.line);
      int regionEnd = max(pos.line, nextEdit.endPos.line);

      int subsetStart = max(0, regionStart - params.motionLinePaddingAbove);
      int subsetEnd = min(currentLines.lastLine(), regionEnd + params.motionLinePaddingBelow);

      // Create subset (small copy - typically 10-20 lines)
      Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);  // +1 for exclusive end

      // Remap positions to subset coordinates
      // Convert half-open endPos to inclusive lastPos for MotionBoundary
      int lineOffset = subsetStart;
      Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);
      Position subsetFirst(nextEdit.beginPos.line - lineOffset, nextEdit.beginPos.col);
      // For pure insertions (beginPos == endPos), use beginPos as both first and last
      Position inclusiveLast = nextEdit.hasDeletedContent()
          ? halfOpenToInclusive(nextEdit.endPos)
          : nextEdit.beginPos;
      Position subsetLast(inclusiveLast.line - lineOffset, inclusiveLast.col);

      // Create boundary for subset, inheriting parent constraints
      // hasLinesAbove = true if subsetStart > 0 OR parent.hasLinesAbove
      // hasLinesBelow = true if subsetEnd < currentLines.lastLine() OR parent.hasLinesBelow
      MotionBoundary subsetBoundary(subset, subsetFirst, subsetLast,
          subsetStart > 0 || boundary.hasLinesAbove(),
          subsetEnd < currentLines.lastLine() || boundary.hasLinesBelow());

      // Use MotionOptimizer to find paths to edit region in bounded subset
      vector<RangeResult> movementResults = motionOptimizer.optimizeToRange(
          subset, subsetPos, s.getRunningEffort(), subsetFirst,
          subsetLast,
          "", // No user sequence reference for sub-optimization
          navigationContext, subsetBoundary, ctx.motionToKeys,
          MotionOptimizerRangeParams{}.withMaxResults(
              clamp(nextEdit.origCharCount(), 1, 10))).results;

      // Remap results back to original coordinates
      for (RangeResult& movResult : movementResults) {
        movResult.goalPos.line += lineOffset;
      }

      // Create new states from movement results
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
