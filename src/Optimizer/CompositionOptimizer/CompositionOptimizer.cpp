#include "CompositionOptimizer.h"

#include <algorithm>
#include <cassert>

#include "CompositionSearchContext.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"

#include "Editor/SequenceParser.h"
#include "Keyboard/KeyedSequence.h"
#include "State/CompositionState.h"
#include "Utils/BracketFlags.h"
#include "Utils/Debug.h"
#include "Utils/QuoteFlags.h"
#include "Utils/StringUtils.h"
#include "VimCore/VimCore.h"

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
CompositionResult CompositionOptimizer::optimize(
    const Lines& initialLines, const Position initialPos, const Lines& goalLines,
    const Position goalPos, CompositionOptimizerParams params,
    string_view userSequence, const MotionBoundary& boundary,
    const NavContext& navigationContext) {
  if(goalPos < initialPos) {
    debug("only support forward motion in CompositionOptimizer");
  }

  MotionOptimizer motionOptimizer(config);

  // Create search context - handles all pre-computation
  CompositionSearchContext ctx(initialLines, initialPos, goalLines, userSequence,
                               navigationContext, boundary,
                               params, config);

  if (ctx.totalEdits == 0) {
    return {};
  }
  if (ctx.totalEdits > 16) {
    debug("Cannot support more than 16 edits");
    return {};
  }

  // Cursor position after last edit completes
  Position resultGoalPos = ctx.editResults.back().goalPos;

  vector<Result> results;

  // Initialize starting state with heuristic cost
  CompositionState startingState(initialPos, Mode::Normal, 0);
  startingState.setCost(ctx.heuristic(startingState, 0));
  ctx.pq.push(startingState);
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  debug("=== CompositionOptimizer A* search ===");

  while (ctx.shouldContinue()) {
    CompositionState s = ctx.popNext();
    Position pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    ctx.markProcessed();

    if (ctx.isGoal(s)) {
      double effort = s.getRunningEffort().getEffort(config);
      debug("GOAL #" + to_string(results.size()) + ":",
            "\"" + s.getMotionSequence() + "\"", "effort:", effort);
      results.emplace_back(s.getMotionSequence(), effort);
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

    debug("pop:", "\"" + s.getMotionSequence() + "\"",
          "pos:", pos, "edits:", editsCompleted,
          "cost:", s.getCost(), "effort:", s.getEffort());

    // Get current buffer state
    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
    const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

    // ========== PURE INSERTION HANDLING ==========
    // Pure insertions have no edit region to transition into.
    // We explore navigation + insertion strategies: o/I/A shortcuts or i fallback.
    if (nextEdit.isPureInsertion()) {
      debug("  pure insertion at", nextEdit.beginPos,
            "text='" + makePrintable(nextEdit.insertedText) + "'");
      const EditResult& editResult = ctx.editResults[editsCompleted];
      Position insertPos = nextEdit.beginPos;
      bool isNewLineInsertion = nextEdit.isNewLineInsertion();

      // Explore an insertion strategy by navigating to a valid range, then inserting.
      // o, I, A -> anywhere in 
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
          Position localRangeEnd(targetLine - beginLine, lastCol + 1);

          // Boundary uses full subset extent, not the target range.
          // The target range is only for optimizeToRange's isInRange check.
          // Using the target range as boundary would clamp motions like $ to the range edge.
          Position subsetFirst(0, 0);
          Position subsetEnd(static_cast<int>(subset.size()) - 1,
              subset.back().effectiveSize());
          MotionBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
              beginLine > 0 || boundary.hasLinesAbove(),
              endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

          vector<RangeResult> results = motionOptimizer.optimizeToRange(
              subset, localPos, localRangeFirst, localRangeEnd,
              MotionOptimizerRangeParams{}
                  .withMaxResults(1)
                  .withMinCountRepeat(params.minCountRepeat), "",
              subsetBoundary, s.getRunningEffort(),
              navigationContext).results;

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
        debug("    exploring o-strategy on line", insertPos.line - 1);
        int targetLine = insertPos.line - 1;
        int lastCol = currentLines[targetLine].empty()
            ? 0 : static_cast<int>(currentLines[targetLine].size()) - 1;
        string_view sourceIndent = VimOptions::autoindent()
            ? leadingWhitespace(currentLines[targetLine])
            : string_view{};
        Lines insertLines = Lines::unflatten(string(nextEdit.insertedTextBody()));
        KeyedSequence typed = buildTypedCommands(insertLines, sourceIndent);
        exploreInsertionStrategy(targetLine, 0, lastCol,
                                 "o" + typed.seq.keys);
      } else {
        // I/A/i: handle autoindent for multi-line insertions
        int fnb = VimCore::firstNonBlankColInLineStr(currentLines[insertPos.line]);
        int lineLen = static_cast<int>(currentLines[insertPos.line].size());
        int lastCol = lineLen == 0 ? 0 : lineLen - 1;
        Lines insertLines = Lines::unflatten(nextEdit.insertedText);

        if (insertPos.col == fnb) {
          debug("    exploring I/i-strategy at fnb col", fnb);
          // I: insert at first non-blank - navigate anywhere on line
          // For multi-line, prefix before cursor is the indent (text before FNB)
          KeyedSequence escaped = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line].substr(0, fnb));
          exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                   "I" + escaped.seq.keys);
          // Also explore i at exact position (cheaper when cursor is already there)
          exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col,
                                   "i" + escaped.seq.keys);
        } else if (insertPos.col == lineLen) {
          debug("    exploring A/a-strategy at eol col", lineLen);
          // A: append at end of line - navigate anywhere on line
          // For multi-line, prefix is the entire current line
          KeyedSequence escaped = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line]);
          exploreInsertionStrategy(insertPos.line, 0, lastCol,
                                   "A" + escaped.seq.keys);
          // Also explore a at last column (cheaper when cursor is already at $)
          exploreInsertionStrategy(insertPos.line, lastCol, lastCol,
                                   "a" + escaped.seq.keys);
        } else {
          debug("    exploring i-strategy at col", insertPos.col);
          // i: fallback - navigate to exact position
          // For multi-line, prefix is text before cursor
          KeyedSequence escaped = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line].substr(0, insertPos.col));
          exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col,
                                   "i" + escaped.seq.keys);
        }
      }
      continue;
    }

    // ========== EDIT vs MOVEMENT TRANSITIONS ==========
    const EditResult& editResult = ctx.editResults[editsCompleted];
    const Result* res = editResult.resultAt(pos.line, pos.col);

    if (res) {
      debug("  edit found at", pos, "seq:", "\"" + res->sequence.keys + "\"",
            "cost:", res->keyCost);
      ctx.exploreEditTransition(s, res->sequence,
                                editResult.goalPos, editsCompleted + 1);
    }

    // J plan: offered from any column on the entry line
    const auto& joinPlan = ctx.joinPlans[editsCompleted];
    if (joinPlan && pos.line == joinPlan->entryLine) {
      debug("  J plan at line", pos.line, "seq:", "\"" + joinPlan->sequence.keys + "\"",
            "effort:", joinPlan->effort);
      ctx.exploreEditTransition(s, joinPlan->sequence, joinPlan->goalPos,
                                editsCompleted + 1);
    }

    if (!res) {
      // Check for bracket/quote text object shortcuts
      // These allow reaching the edit region from positions before it on the same line
      const BracketQuoteContext& bqContext = ctx.bracketQuoteContexts[editsCompleted];
      if (bqContext.line == pos.line) {
        debug("  checking text objects at col", pos.col, "on line", pos.line);
        const EditResult& editResult = ctx.editResults[editsCompleted];
        const string& insertedText = nextEdit.insertedText;

        if (pos.col < static_cast<int>(bqContext.validQuoteMask.size())) {
          for (char q : QuoteFlags::ALL_QUOTES) {
            if (bqContext.validQuoteMask[pos.col].seen(q)) {
              // Build sequence: c + i/a + quote + insertedText + <Esc>
              string seq = string("c") + bqContext.quoteModifier(q) + q + insertedText + "<Esc>";
              debug("    quote textobj:", string(1, bqContext.quoteModifier(q)) + q);
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
              debug("    bracket textobj:", string(1, bqContext.bracketModifier(b)) + b);
              ctx.exploreEditTransition(s, Sequence(seq), editResult.goalPos,
                                        editsCompleted + 1);
            }
          }
        }
      }

      // If cursor is already inside the edit range but no edit result was found
      // (e.g. maxResults budget exhausted), skip motion search — we're already there.
      if (pos >= nextEdit.beginPos && pos < nextEdit.endPos) {
        debug("  inside edit range but no result at", pos, "- skipping");
        continue;
      }

      // Slice a padded subset around [pos, edit region] for MotionOptimizer
      auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
          min(pos.line, nextEdit.beginPos.line),
          max(pos.line, nextEdit.endPos.line),
          params.motionPaddingAbove, params.motionPaddingBelow);

      Lines subset = currentLines.getLineRange(beginLine, endLine);

      Position localPos(pos.line - beginLine, pos.col, pos.targetCol);
      Position localRangeFirst(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
      Position localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

      // Boundary uses full subset extent, not the edit range.
      // The edit range is only the target for optimizeToRange's isInRange check.
      // Using the edit range as boundary would clamp motions like $ to the range edge.
      Position subsetFirst(0, 0);
      Position subsetEnd(static_cast<int>(subset.size()) - 1,
          subset.back().effectiveSize());
      MotionBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
          beginLine > 0 || boundary.hasLinesAbove(),
          endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

      debug("  motion search from", pos, "to edit region [" +
            to_string(nextEdit.beginPos.line) + "," + to_string(nextEdit.beginPos.col)
            + ")-[" + to_string(nextEdit.endPos.line) + "," +
            to_string(nextEdit.endPos.col) + ")");

      vector<RangeResult> movementResults = motionOptimizer.optimizeToRange(
          subset, localPos, localRangeFirst, localRangeEnd,
          MotionOptimizerRangeParams{}
              .withMaxResults(clamp(nextEdit.origCharCount(), 1, 10))
              .withMinCountRepeat(params.minCountRepeat), "",
          subsetBoundary, s.getRunningEffort(),
          navigationContext).results;

      debug("  motion results:", static_cast<int>(movementResults.size()));
      for (RangeResult& movResult : movementResults) {
        if (!movResult.isValid()) continue;

        // Remap results back to full-buffer coordinates
        movResult.goalPos.line += beginLine;
        debug("    motion:", "\"" + movResult.sequence.keys + "\"",
              "->", movResult.goalPos);
        ctx.exploreMotionTransition(s, movResult.sequence, movResult.goalPos,
                                    editsCompleted);
      }

      // If a J plan exists for this edit and cursor isn't on the entry line,
      // also search for motions to the J plan's entry line (full line range).
      // This handles cases where the edit region is unreachable (e.g., \n → space)
      // but J can fire from anywhere on the entry line.
      if (joinPlan && pos.line != joinPlan->entryLine) {
        int jLine = joinPlan->entryLine;
        int jLineLen = currentLines[jLine].effectiveSize();
        Position jRangeFirst(jLine, 0);
        Position jRangeEnd(jLine, jLineLen);

        auto [jBeginLine, jEndLine] = currentLines.minmaxBoundWithPadding(
            min(pos.line, jLine), max(pos.line, jLine),
            params.motionPaddingAbove, params.motionPaddingBelow);

        Lines jSubset = currentLines.getLineRange(jBeginLine, jEndLine);
        Position jLocalPos(pos.line - jBeginLine, pos.col, pos.targetCol);
        Position jLocalFirst(jLine - jBeginLine, 0);
        Position jLocalEnd(jLine - jBeginLine, jLineLen);

        Position jSubsetFirst(0, 0);
        Position jSubsetEnd(static_cast<int>(jSubset.size()) - 1,
            jSubset.back().effectiveSize());
        MotionBoundary jSubsetBoundary(jSubset, jSubsetFirst, jSubsetEnd,
            jBeginLine > 0 || boundary.hasLinesAbove(),
            jEndLine <= currentLines.lastLine() || boundary.hasLinesBelow());

        vector<RangeResult> jMovResults = motionOptimizer.optimizeToRange(
            jSubset, jLocalPos, jLocalFirst, jLocalEnd,
            MotionOptimizerRangeParams{}
                .withMaxResults(1)
                .withMinCountRepeat(params.minCountRepeat), "",
            jSubsetBoundary, s.getRunningEffort(),
            navigationContext).results;

        for (RangeResult& movResult : jMovResults) {
          if (!movResult.isValid()) continue;
          movResult.goalPos.line += jBeginLine;
          debug("    J-motion:", "\"" + movResult.sequence.keys + "\"",
                "->", movResult.goalPos);
          ctx.exploreMotionTransition(s, movResult.sequence, movResult.goalPos,
                                      editsCompleted);
        }
      }
    }
  }

  debug("=== CompositionOptimizer done ===");
  debug("results:", static_cast<int>(results.size()),
        "nodes:", ctx.nodesProcessed,
        "pops:", ctx.totalPops,
        "skipped:", ctx.statesSkipped,
        "queueRemaining:", static_cast<int>(ctx.pq.size()));

  return {std::move(results), ctx.getStats(static_cast<int>(results.size())),
          resultGoalPos, std::move(ctx.diffStates)};
}

ostream& operator<<(ostream& os, const CompositionResult& cr) {
  os << cr.stats << " goalPos=" << cr.goalPos << "\n";

  // Print diff legend: all diffs get sequential {n} labels.
  if (!cr.diffs.empty()) {
    os << "Diffs:";
    for (size_t i = 0; i < cr.diffs.size(); i++) {
      const auto& d = cr.diffs[i];
      os << " {" << i << "}=";
      if (d.isPureInsertion()) {
        os << "ins '" << makePrintable(d.insertedText) << "'";
      } else if (d.isPureDeletion()) {
        os << "del '" << makePrintable(d.deletedText) << "'";
      } else {
        os << "'" << makePrintable(d.deletedText) << "'->'" << makePrintable(d.insertedText) << "'";
      }
    }
    os << "\n";
  }

  // Print each result with edit operations replaced by {n} placeholders.
  // diffIdx tracks which diff we're on: advances on Delete tokens that match
  // pure deletion diffs, and on TypedText tokens (replacements/insertions).
  for (size_t i = 0; i < cr.results.size(); i++) {
    os << "  [" << i << "] ";

    vector<SequenceToken> tokens = parseSequence(cr.results[i].sequence.keys);
    int diffIdx = 0;
    int numDiffs = static_cast<int>(cr.diffs.size());
    for (size_t j = 0; j < tokens.size(); j++) {
      auto type = tokens[j].type;

      // Spacing before this token
      if (j > 0) {
        auto prev = tokens[j - 1].type;
        if (prev == TokenType::Escape || prev == TokenType::Delete ||
            type == TokenType::Delete ||
            (prev == TokenType::Change && type == TokenType::TypedText)) {
          os << " ";
        }
      }

      if (type == TokenType::Delete &&
          diffIdx < numDiffs && cr.diffs[diffIdx].isPureDeletion()) {
        // Pure deletion diff: show command as-is, advance diffIdx
        os << makePrintable(tokens[j].text);
        diffIdx++;
      } else if (type == TokenType::TypedText && diffIdx < numDiffs) {
        // Replacement/insertion: strip leading control chars (<BS>, <Del>)
        // and show them before the {n} placeholder.
        string_view text = tokens[j].text;
        while (text.starts_with("<BS>") || text.starts_with("<Del>")) {
          if (text.starts_with("<BS>")) {
            os << "<BS>";
            text.remove_prefix(4);
          } else {
            os << "<Del>";
            text.remove_prefix(5);
          }
        }
        os << "{" << diffIdx++ << "}";
      } else {
        os << makePrintable(tokens[j].text);
      }
    }

    os << " " << cr.results[i].keyCost << "\n";
  }

  return os;
}
