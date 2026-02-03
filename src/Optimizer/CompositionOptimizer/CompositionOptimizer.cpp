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
  if (insertedText.empty()) {
    return insertPos;
  }
  Lines inserted = Lines::unflatten(insertedText);
  if (inserted.size() == 1) {
    // Single line: cursor at last char
    int endCol = insertPos.col + static_cast<int>(inserted[0].size()) - 1;
    return Position(insertPos.line, max(0, endCol));
  } else {
    // Multi-line: cursor at last char of last line
    int lastLine = insertPos.line + static_cast<int>(inserted.size()) - 1;
    int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
    return Position(lastLine, lastCol);
  }
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
                               params, config, overshootPenalty, forwardBias,
                               maxLineLength);

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

    // ========== PURE INSERTION TRANSITIONS ==========
    // Pure insertions have no EditResult - handle specially here
    if (ctx.isPureInsertion(editsCompleted)) {
      Position insertPos = nextEdit.firstPos;
      const string& text = nextEdit.insertedText;

      // Detect if this is a "new line" insertion (text ends with \n) vs inline insertion
      // o/O create the newline themselves, so strip trailing \n from text
      bool isNewLineInsertion = !text.empty() && text.back() == '\n';
      string textContent = isNewLineInsertion
          ? text.substr(0, text.size() - 1)
          : text;

      // Compute end position after insertion completes
      Position goalPos = computeInsertEndPos(insertPos, text);

      // Strategy 1: o/O shortcuts (ONLY for new line insertions at col 0)
      if (isNewLineInsertion && insertPos.col == 0) {
        // `o` from line N-1 creates new line at N (inserts below current line)
        if (pos.line == insertPos.line - 1 && insertPos.line > 0) {
          string seq = "o" + textContent + "<Esc>";
          CompositionState newState = s.afterEditTransition(
              {Sequence(seq, Mode::Normal)}, goalPos, Mode::Normal, config);
          newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
          ctx.exploreNewState(std::move(newState));
        }
        // `O` from line N creates new line at N (inserts above current line)
        if (pos.line == insertPos.line) {
          string seq = "O" + textContent + "<Esc>";
          CompositionState newState = s.afterEditTransition(
              {Sequence(seq, Mode::Normal)}, goalPos, Mode::Normal, config);
          newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
          ctx.exploreNewState(std::move(newState));
        }
      }

      // Strategy 2: Already at insertion point - use i
      if (pos == insertPos) {
        string seq = "i" + text + "<Esc>";
        CompositionState newState = s.afterEditTransition(
            {Sequence(seq, Mode::Normal)}, goalPos, Mode::Normal, config);
        newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
        ctx.exploreNewState(std::move(newState));
      }

      // Strategy 3: I/A shortcuts (for inline insertions on same line)
      // These insert INTO existing line, NOT for creating new lines
      if (pos.line == insertPos.line && !isNewLineInsertion) {
        int fnb = VimCore::firstNonBlankColInLineStr(currentLines[pos.line]);
        int lineEnd = static_cast<int>(currentLines[pos.line].size());

        // I gets to first non-blank
        if (insertPos.col == fnb) {
          string seq = "I" + text + "<Esc>";
          CompositionState newState = s.afterEditTransition(
              {Sequence(seq, Mode::Normal)}, goalPos, Mode::Normal, config);
          newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
          ctx.exploreNewState(std::move(newState));
        }
        // A appends at end of line
        if (insertPos.col == lineEnd) {
          string seq = "A" + text + "<Esc>";
          CompositionState newState = s.afterEditTransition(
              {Sequence(seq, Mode::Normal)}, goalPos, Mode::Normal, config);
          newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
          ctx.exploreNewState(std::move(newState));
        }
      }

      // Strategy 4: Need full movement - use MotionOptimizer to reach insertPos
      if (pos != insertPos) {
        // For pure insertions, firstPos == lastPos, so use a simple point target
        int regionStart = min(pos.line, insertPos.line);
        int regionEnd = max(pos.line, insertPos.line);

        int subsetStart = max(0, regionStart - params.motionLinePaddingAbove);
        int subsetEnd = min(currentLines.lastLine(), regionEnd + params.motionLinePaddingBelow);

        Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);
        int lineOffset = subsetStart;
        Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);
        Position subsetInsert(insertPos.line - lineOffset, insertPos.col);

        MotionBoundary subsetBoundary(subset, subsetInsert, subsetInsert,
            subsetStart > 0 || boundary.hasLinesAbove(),
            subsetEnd < currentLines.lastLine() || boundary.hasLinesBelow());

        // Use MotionOptimizer to find paths to insertion point
        vector<RangeResult> movementResults = motionOptimizer.optimizeToRange(
            subset, subsetPos, s.getRunningEffort(), subsetInsert, subsetInsert,
            "", navigationContext, subsetBoundary, ctx.motionToKeys,
            MotionOptimizerRangeParams{}.withMaxResults(3)).results;

        for (RangeResult& movResult : movementResults) {
          movResult.goalPos.line += lineOffset;
        }

        for (const RangeResult& movResult : movementResults) {
          if (!movResult.isValid())
            continue;

          CompositionState newState = s.afterMotionResult(
              movResult.sequences, movResult.goalPos, config);
          newState.setCost(ctx.heuristic(newState, editsCompleted));
          ctx.exploreNewState(std::move(newState));
        }
      }

      continue;  // Skip normal edit/movement handling
    }

    // ========== EDIT TRANSITIONS ==========
    // Check if we can perform the next edit from current position
    // Uses unified check: flatIndexAt encodes the same position validity as the old bitmask
    bool editTransitionTaken = false;
    if (!ctx.isPureInsertion(editsCompleted)) {
      const EditResult& editResult = ctx.getEditResult(editsCompleted);

      // Convert buffer position to edit region index (O(1) lookup)
      // flatIndexAt returns -1 for out-of-region lines, and we bounds-check for columns
      int flatIdx = editResult.flatIndexAt(pos);
      if (flatIdx >= 0 &&
          flatIdx < static_cast<int>(editResult.typeAllResults.size())) {
        const Result& editRes = editResult.typeAllResults[flatIdx];
        if (editRes.isValid()) {
          editTransitionTaken = true;
          // Create new state with edit transition applied
          // Cursor ends at last char of inserted text (precomputed in editResult.goalPos)
          CompositionState newState = s.afterEditTransition(
              editRes.sequences, editResult.goalPos, Mode::Normal, config);
          newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
          ctx.exploreNewState(std::move(newState));
        }
      }
    }

    // ========== MOVEMENT TRANSITIONS ==========
    // Use MotionOptimizer to find optimal paths to next edit region
    if (!editTransitionTaken) {
      // TODO: Check for bracket/quote motions first

      // Calculate line subset bounds for MotionOptimizer
      // Include padding to allow overshoot-and-return paths
      int regionStart = min(pos.line, nextEdit.firstPos.line);
      int regionEnd = max(pos.line, nextEdit.lastPos.line);

      int subsetStart = max(0, regionStart - params.motionLinePaddingAbove);
      int subsetEnd = min(currentLines.lastLine(), regionEnd + params.motionLinePaddingBelow);

      // Create subset (small copy - typically 10-20 lines)
      Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);  // +1 for exclusive end

      // Remap positions to subset coordinates
      int lineOffset = subsetStart;
      Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);
      Position subsetFirst(nextEdit.firstPos.line - lineOffset, nextEdit.firstPos.col);
      Position subsetLast(nextEdit.lastPos.line - lineOffset, nextEdit.lastPos.col);

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

        CompositionState newState = s.afterMotionResult(
            movResult.sequences, movResult.goalPos, config);
        newState.setCost(ctx.heuristic(newState, editsCompleted));
        ctx.exploreNewState(std::move(newState));
      }
    }
  }

  return results;
}
