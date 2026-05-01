#include "CompositionOptimizer.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <optional>

#include "CompositionSearchContext.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"

#include "Interpreter/SequenceParser.h"
#include "Keyboard/KeyedSequence.h"
#include "Optimizer/CompositionOptimizer/CompositionState.h"
#include "Types/BracketFlags.h"
#include "Utils/Debug.h"
#include "Optimizer/Indentation.h"
#include "Types/QuoteFlags.h"
#include "Utils/StringUtils.h"
#include "VimCore/VimCore.h"

using namespace std;

namespace {

// Compute cursor position after insertion (same for i/a/o/O/I/A + text + Esc)
// Cursor ends on last char of inserted text, or stays at insertPos if empty
CursorPos computeInsertEndPos(CursorPos insertPos, const string& insertedText) {
  assert(!insertedText.empty());
  Lines inserted = Lines::unflatten(insertedText);
  if (inserted.size() == 1) {
    int endCol = insertPos.col + static_cast<int>(inserted[0].size()) - 1;
    return CursorPos(insertPos.line, max(0, endCol));
  } else {
    int lastLine = insertPos.line + static_cast<int>(inserted.size()) - 1;
    int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
    return CursorPos(lastLine, lastCol);
  }
}

// Clamp cursor to a valid normal-mode position on concrete post-edit lines.
// Uses targetCol when available so vertical sticky-column intent is preserved.
CursorPos clampGoalPosToLines(const CursorPos& pos, const Lines& lines) {
  if (lines.empty()) return CursorPos(0, 0);

  int line = clamp(pos.line, 0, lines.lastLine());
  int wantedCol = pos.targetCol >= 0 ? pos.targetCol : pos.col;
  int maxCol = lines[line].empty() ? 0 : static_cast<int>(lines[line].size()) - 1;
  int col = clamp(wantedCol, 0, maxCol);
  return CursorPos(line, col, wantedCol);
}

} // anonymous namespace

// `goalPos` is a real terminal target: the search continues past the last edit
// with motion-only transitions until pos == goalPos. For pure-motion sessions
// (E == 0), the search starts directly in the post-final-edit nav phase.
CompositionResult CompositionOptimizer::optimize(
    const Lines& initialLines, const CursorPos initialPos, const Lines& goalLines,
    const CursorPos goalPos, CompositionOptimizerParams params,
    string_view userSequence, const NavBoundary& boundary,
    const NavContext& navigationContext) {
  CHECK(goalLines.contains(goalPos),
        "goalPos must be a valid normal-mode cursor position in goalLines");
  if(goalPos < initialPos) {
    debug("only support forward motion in CompositionOptimizer");
  }

  NavOptimizer navOptimizer(config);

  // Create search context - handles all pre-computation
  CompositionSearchContext ctx(initialLines, initialPos, goalLines, goalPos,
                               userSequence, navigationContext, boundary,
                               params, config);

  if (ctx.totalEdits() > 16) {
    debug("Cannot support more than 16 edits");
    return {};
  }

  vector<Result> results;

  // Initialize starting state with heuristic cost
  CompositionState startingState(initialPos, Mode::Normal, 0);
  startingState.setCost(ctx.heuristic(startingState, 0));
  ctx.pq.push(startingState);
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  auto enqueueState = [&](CompositionState&& newState) {
    if (newState.getEffort() > ctx.maxEffort) {
      debug("  pruned (effort", newState.getEffort(), ">", ctx.maxEffort, "):",
            "\"" + newState.getSequence().str() + "\"");
      return;
    }

    double newCost = newState.getCost();
    const CompositionStateKey newKey = newState.getKey();
    // Terminal states (all edits done AND cursor at goalPos) are not cached so
    // multiple distinct paths to goal can each emit a result. Post-final-edit
    // nav states (all edits done but cursor still moving toward goalPos) ARE
    // cached, same as inter-edit nav, to avoid redundant exploration.
    const bool isTerminal = (newState.getEditsCompleted() == ctx.totalEdits() &&
                             newState.getPos() == ctx.goalPos);
    auto it = ctx.costMap.find(newKey);

    if (it == ctx.costMap.end()) {
      if (!isTerminal) {
        ctx.costMap.emplace(newKey, newCost);
      }
      ctx.pq.push(std::move(newState));
    } else if (newCost <= it->second) {
      it->second = newCost;
      ctx.pq.push(std::move(newState));
    } else {
      debug("  not enqueued (cost", newCost, ">=", it->second, "):",
            "\"" + newState.getSequence().str() + "\"");
    }
  };
  auto enqueueEditTransition = [&](const CompositionState& current,
                                   const Sequence& editSequence,
                                   const CursorPos& goalPos,
                                   int editsAfter) {
    CompositionState newState = current.afterEditTransition(
        editSequence, goalPos, Mode::Normal, config);
    newState.setCost(ctx.heuristic(newState, editsAfter));
    enqueueState(std::move(newState));
  };
  auto enqueueMotionTransition = [&](const CompositionState& current,
                                     const Sequence& moveSequence,
                                     const CursorPos& goalPos,
                                     int editsCompleted) {
    CompositionState newState = current.afterNavResult(
        moveSequence, goalPos, config);
    newState.setCost(ctx.heuristic(newState, editsCompleted));
    enqueueState(std::move(newState));
  };

  // Slice a padded subset of `fromLines` covering the cursor and target lines,
  // and build the boundary the inner NavOptimizer will see. Used by both the
  // range-motion and point-motion exploration helpers below.
  auto sliceMotionSubset = [&](CursorPos pos, int targetBeginLine, int targetEndLine,
                               const Lines& fromLines) {
    struct Slice {
      Lines subset;
      int beginLine;
      int endLine;
      CursorPos localPos;
      NavBoundary subsetBoundary;
    };
    auto [beginLine, endLine] = fromLines.minmaxBoundWithPadding(
        min(pos.line, targetBeginLine), max(pos.line + 1, targetEndLine + 1),
        params.navPaddingAbove, params.navPaddingBelow);
    Lines subset = fromLines.getLineRange(beginLine, endLine);
    CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
    CursorPos subsetFirst(0, 0);
    CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
        subset.back().effectiveSize());
    NavBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
        beginLine > 0 || boundary.hasLinesAbove(),
        endLine <= fromLines.lastLine() || boundary.hasLinesBelow());
    return Slice{std::move(subset), beginLine, endLine, localPos, std::move(subsetBoundary)};
  };

  // Enumerate motions from `pos` toward a CharInterval target.
  // `makeLocalInterval(subset, beginLine)` translates the caller's full-buffer
  // target intent into subset-local coords (returns nullopt to skip search).
  // - Multi-sink range targets (inter-edit motion, J-plan motion) use the
  //   default `allowMultiplePerPosition=false` so we get one cheapest path
  //   per landing.
  // - Single-cursor targets (post-final-edit nav) construct a 1-element
  //   interval and pass `allowMultiplePerPosition=true` so multiple distinct
  //   sequences to the single point are enumerated.
  auto exploreMotionsToInterval = [&](
      const CompositionState& s, CursorPos pos,
      int targetBeginLine, int targetEndLine,
      const Lines& fromLines, int editsCompleted,
      int maxResults, bool allowMultiplePerPosition,
      auto&& makeLocalInterval) {
    auto slice = sliceMotionSubset(pos, targetBeginLine, targetEndLine, fromLines);
    auto localInterval = makeLocalInterval(slice.subset, slice.beginLine);
    if (!localInterval) return;

    auto navParams = NavOptimizerParams{}
        .withMaxResults(maxResults)
        .withMinCountRepeat(params.minPrefixCount)
        .withMaxCountRepeat(params.maxPrefixCount)
        .withAllowMultiplePerPosition(allowMultiplePerPosition);
    const BufferIndex* bufferIndex = nullptr;
    int lineOffset = 0;
    bool hasBufferIndex = ctx.tryGetBufferIndex(
        editsCompleted, slice.beginLine, slice.endLine, bufferIndex, lineOffset);
    auto navResult = hasBufferIndex
        ? navOptimizer.optimize(
              slice.subset, slice.localPos, *localInterval,
              navParams, "", slice.subsetBoundary,
              navigationContext, *bufferIndex, lineOffset)
        : navOptimizer.optimize(
              slice.subset, slice.localPos, *localInterval,
              navParams, "", slice.subsetBoundary, navigationContext);
    ctx.navNodesExplored += navResult.getStats().nodesExplored();

    for (const LandingResult& movResult : navResult.getResults()) {
      if (movResult.getSequence().empty()) continue;
      CursorPos goalPos = movResult.getGoalPos();
      goalPos.line += slice.beginLine;
      enqueueMotionTransition(s, movResult.getSequence(), goalPos, editsCompleted);
    }
  };

  debug("=== CompositionOptimizer A* search ===");

  while (!ctx.pq.empty() && ctx.totalPops < params.maxNodesPopped) {
    CompositionState s = ctx.pq.top();
    ctx.pq.pop();
    ctx.totalPops++;
    CursorPos pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    if (editsCompleted == ctx.totalEdits() && pos == goalPos) {
      ctx.nodesProcessed++;
      double effort = s.getRunningEffort().getEffort(config);
      debug("GOAL #" + to_string(results.size()) + ":",
            "\"" + s.getSequence().str() + "\"", "effort:", effort);
      results.emplace_back(s.getSequence().str(), effort);
      if (results.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    }

    auto costIt = ctx.costMap.find(s.getKey());
    if (costIt != ctx.costMap.end() && costIt->second < s.getCost()) {
      ctx.statesSkipped++;
      continue;
    }

    ctx.nodesProcessed++;
    ctx.trackState(s);

    debug("pop:", "\"" + s.getSequence().str() + "\"",
          "pos:", pos, "edits:", editsCompleted,
          "cost:", s.getCost(), "effort:", s.getEffort());

    // ========== POST-FINAL-EDIT NAV PHASE ==========
    // All edits applied but cursor isn't at goalPos yet. Enumerate motions
    // from `pos` toward `goalPos` and enqueue, keeping editsCompleted fixed
    // at totalEdits(). No edit transitions are explored here.
    if (editsCompleted == ctx.totalEdits()) {
      debug("  post-edit nav from", pos, "to goalPos", goalPos);
      exploreMotionsToInterval(
          s, pos, goalPos.line, goalPos.line,
          ctx.getLinesAfter(editsCompleted), editsCompleted,
          clamp(params.maxResults, 1, 10), /*allowMultiplePerPosition=*/true,
          [&](const Lines& subset, int beginLine) -> std::optional<CharInterval> {
            CursorPos localGoal(goalPos.line - beginLine, goalPos.col, goalPos.targetCol);
            return CharInterval(localGoal, localGoal);
          });
      continue;
    }

    // Get current buffer state
    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
    const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

    // ========== PURE INSERTION HANDLING ==========
    // Pure insertions have no edit region to transition into.
    // We explore navigation + insertion strategies: o/I/A shortcuts or i fallback.
    if (nextEdit.isPureInsertion()) {
      debug("  pure insertion at", nextEdit.beginPos,
            "text='" + makePrintable(nextEdit.insertedText) + "'");
      const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
      CursorPos insertPos = nextEdit.beginPos;
      bool isNewLineInsertion = nextEdit.isNewLineInsertion();

      // Explore an insertion strategy by navigating to a valid half-open target range,
      // then inserting.
      auto exploreInsertionStrategy = [&](int targetLine, int beginCol, int endCol,
                                          const string& insertCmd) {
        bool inRange = (pos.line == targetLine &&
                        pos.col >= beginCol && pos.col < endCol);

        if (inRange) {
          enqueueEditTransition(s, Sequence(insertCmd), transformResult.getGoalPos(),
                                editsCompleted + 1);
        } else {
          // Slice a padded subset around [pos, target] for NavOptimizer
          auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
              min(pos.line, targetLine), max(pos.line, targetLine) + 1,
              params.navPaddingAbove, params.navPaddingBelow);

          Lines subset = currentLines.getLineRange(beginLine, endLine);

          // Remap positions to subset-local coordinates
          CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
          CursorPos localRangeBegin(targetLine - beginLine, beginCol);
          CursorPos localRangeEnd(targetLine - beginLine, endCol);

          // Boundary uses full subset extent, not the target range.
          // The target range is only for the range-goal `optimize` isInRange
          // check. Using it as boundary would clamp motions like $ to the
          // range edge.
          CursorPos subsetFirst(0, 0);
          CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
              subset.back().effectiveSize());
          NavBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
              beginLine > 0 || boundary.hasLinesAbove(),
              endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

          auto rangeParams = NavOptimizerParams{}
              .withMaxResults(1)
              .withMinCountRepeat(params.minPrefixCount)
              .withMaxCountRepeat(params.maxPrefixCount);
          const BufferIndex* bufferIndex = nullptr;
          int lineOffset = 0;
          bool hasBufferIndex = ctx.tryGetBufferIndex(
              editsCompleted, beginLine, endLine, bufferIndex, lineOffset);
          CharInterval motionRange = toMotionInterval(
              subset, CharRange(localRangeBegin, localRangeEnd));
          auto navResult = hasBufferIndex
              ? navOptimizer.optimize(
                    subset, localPos, motionRange,
                    rangeParams, "", subsetBoundary,
                    navigationContext, *bufferIndex, lineOffset)
              : navOptimizer.optimize(
                    subset, localPos, motionRange,
                    rangeParams, "", subsetBoundary,
                    navigationContext);
          ctx.navNodesExplored += navResult.getStats().nodesExplored();

          for (const LandingResult& movResult : navResult.getResults()) {
            if (movResult.getSequence().empty()) continue;

            const CursorPos& localGoal = movResult.getGoalPos();
            assert(localGoal >= localRangeBegin && localGoal < localRangeEnd &&
                   "pure insertion motion goal must be subset-local and inside target range");
            // Intentionally do not remap localGoal to full-buffer coordinates here:
            // this branch immediately appends the insertion and transitions using
            // transformResult.getGoalPos(), so intermediate motion endpoint isn't consumed.
            Sequence fullSeq = movResult.getSequence();
            fullSeq.append(insertCmd);
            enqueueEditTransition(s, fullSeq, transformResult.getGoalPos(),
                                  editsCompleted + 1);
          }
        }
      };

      // o: skip the trailing newline since the command opens a new line
      if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
        debug("    exploring o-strategy on line", insertPos.line - 1);
        int targetLine = insertPos.line - 1;
        int lineEnd = currentLines[targetLine].effectiveSize();
        string_view sourceIndent = VimOptions::autoindent()
            ? leadingWhitespace(currentLines[targetLine])
            : string_view{};
        Lines insertLines = Lines::unflatten(string(nextEdit.insertedTextBody()));
        KeyedSequence typed = buildTypedCommands(insertLines, sourceIndent);
        exploreInsertionStrategy(targetLine, 0, lineEnd,
                                 "o" + typed.seq.str());
      } else {
        // I/A/i: handle autoindent for multi-line insertions
        int fnb = VimCore::firstNonBlankColInLineStr(currentLines[insertPos.line]);
        int lineLen = static_cast<int>(currentLines[insertPos.line].size());
        int lineEnd = currentLines[insertPos.line].effectiveSize();
        int lastContentCol = lineEnd - 1;
        Lines insertLines = Lines::unflatten(nextEdit.insertedText);

        if (insertPos.col == fnb) {
          debug("    exploring I/i-strategy at fnb col", fnb);
          // I: insert at first non-blank - navigate anywhere on line
          // For multi-line, prefix before cursor is the indent (text before FNB)
          KeyedSequence escaped = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line].substr(0, fnb));
          exploreInsertionStrategy(insertPos.line, 0, lineEnd,
                                   "I" + escaped.seq.str());
          // Also explore i at exact position (cheaper when cursor is already there)
          exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col + 1,
                                   "i" + escaped.seq.str());
        } else if (insertPos.col == lineLen) {
          debug("    exploring A/a-strategy at eol col", lineLen);
          // A: append at end of line - navigate anywhere on line
          // For multi-line, prefix is the entire current line
          KeyedSequence escaped = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line]);
          exploreInsertionStrategy(insertPos.line, 0, lineEnd,
                                   "A" + escaped.seq.str());
          // Also explore a at last column (cheaper when cursor is already at $)
          exploreInsertionStrategy(insertPos.line, lastContentCol, lastContentCol + 1,
                                   "a" + escaped.seq.str());
        } else {
          debug("    exploring i-strategy at col", insertPos.col);
          // i: fallback - navigate to exact position
          // For multi-line, prefix is text before cursor
          KeyedSequence escaped = buildTypedCommands(insertLines, "",
              currentLines[insertPos.line].substr(0, insertPos.col));
          exploreInsertionStrategy(insertPos.line, insertPos.col, insertPos.col + 1,
                                   "i" + escaped.seq.str());
        }
      }
      continue;
    }

    // ========== EDIT vs MOVEMENT TRANSITIONS ==========
    const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
    auto editAlternatives = transformResult.resultsAt(pos.line, pos.col);

    for (const Result& res : editAlternatives) {
      CursorPos editGoalPos = transformResult.goalPosAt(pos.line, pos.col);
      if (transformResult.hasPerStartGoals()) {
        editGoalPos = clampGoalPosToLines(editGoalPos, ctx.getLinesAfter(editsCompleted + 1));
      }
      debug("  edit found at", pos, "seq:", "\"" + res.getSequence().str() + "\"",
            "cost:", res.getCost(), "goalPos:", editGoalPos);
      enqueueEditTransition(s, res.getSequence(),
                            editGoalPos, editsCompleted + 1);
    }

    // J plan: offered from any column on the entry line
    const auto& joinPlan = ctx.edits[editsCompleted].joinPlan;
    if (joinPlan && pos.line == joinPlan->entryLine) {
      debug("  J plan at line", pos.line, "seq:", "\"" + joinPlan->sequence.str() + "\"",
            "effort:", joinPlan->effort);
      enqueueEditTransition(s, joinPlan->sequence, joinPlan->goalPos,
                            editsCompleted + 1);
    }

    if (editAlternatives.empty()) {
      // Check for bracket/quote text object shortcuts
      // These allow reaching the edit region from positions before it on the same line
      const BracketQuoteContext& bqContext = ctx.edits[editsCompleted].bracketQuoteContext;
      if (bqContext.line == pos.line) {
        debug("  checking text objects at col", pos.col, "on line", pos.line);
        const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
        const string& insertedText = nextEdit.insertedText;
        bool pureDeletion = nextEdit.isPureDeletion();
        char textObjOp = pureDeletion ? 'd' : 'c';

        if (pos.col < static_cast<int>(bqContext.validQuoteMask.size())) {
          for (char q : QuoteFlags::ALL_QUOTES) {
            if (bqContext.validQuoteMask[pos.col].seen(q)) {
              // Build sequence:
              // - pure deletion: d + i/a + quote
              // - replacement/edit: c + i/a + quote + insertedText + <Esc>
              string seq = string(1, textObjOp) + bqContext.quoteModifier(q) + q;
              if (!pureDeletion) {
                seq += insertedText;
                seq += "<Esc>";
              }
              debug("    quote textobj:", string(1, bqContext.quoteModifier(q)) + q);
              enqueueEditTransition(s, Sequence(seq), transformResult.getGoalPos(),
                                    editsCompleted + 1);
            }
          }
        }
        if (pos.col < static_cast<int>(bqContext.validBracketMask.size())) {
          for (char b : BracketFlags::ALL_BRACKETS) {
            if (bqContext.validBracketMask[pos.col].seen(b)) {
              // Build sequence:
              // - pure deletion: d + i/a + bracket
              // - replacement/edit: c + i/a + bracket + insertedText + <Esc>
              string seq = string(1, textObjOp) + bqContext.bracketModifier(b) + b;
              if (!pureDeletion) {
                seq += insertedText;
                seq += "<Esc>";
              }
              debug("    bracket textobj:", string(1, bqContext.bracketModifier(b)) + b);
              enqueueEditTransition(s, Sequence(seq), transformResult.getGoalPos(),
                                    editsCompleted + 1);
            }
          }
        }
      }

      // If cursor is already inside the edit range but no edit result was found
      // (e.g. maxResults budget exhausted), skip motion search — we're already there.
      if (nextEdit.contains(pos)) {
        debug("  inside edit range but no result at", pos, "- skipping");
        continue;
      }

      debug("  motion search from", pos, "to edit region [" +
            to_string(nextEdit.beginPos.line) + "," + to_string(nextEdit.beginPos.col)
            + ")-[" + to_string(nextEdit.endPos.line) + "," +
            to_string(nextEdit.endPos.col) + ")");

      // Motions to the next edit's diff range.
      exploreMotionsToInterval(
          s, pos, nextEdit.beginPos.line, nextEdit.editEndLine() - 1,
          currentLines, editsCompleted,
          clamp(nextEdit.origCharCount(), 1, 10),
          /*allowMultiplePerPosition=*/false,
          [&](const Lines& subset, int beginLine) -> std::optional<CharInterval> {
            CursorPos localBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
            CursorPos localEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);
            return tryToMotionInterval(subset, CharRange(localBegin, localEnd));
          });

      // J plan: if cursor isn't on the entry line, also search for motions to
      // the entry line (whole-line range). This handles cases where the edit
      // region is unreachable (e.g., \n → space) but J can fire from anywhere
      // on the entry line.
      if (joinPlan && pos.line != joinPlan->entryLine) {
        const int jLine = joinPlan->entryLine;
        exploreMotionsToInterval(
            s, pos, jLine, jLine, currentLines, editsCompleted, /*maxResults=*/1,
            /*allowMultiplePerPosition=*/false,
            [&](const Lines& subset, int beginLine) -> std::optional<CharInterval> {
              return wholeLineMotionInterval(subset, jLine - beginLine);
            });
      }
    }
  }

  debug("=== CompositionOptimizer done ===");
  debug("results:", static_cast<int>(results.size()),
        "nodes:", ctx.nodesProcessed,
        "pops:", ctx.totalPops,
        "skipped:", ctx.statesSkipped,
        "queueRemaining:", static_cast<int>(ctx.pq.size()));

  // Extract the persistent planned-edit breakdown and edit-scoped artifacts.
  std::vector<Lines> fenceposts;
  std::vector<DiffState> diffs;
  std::vector<TransformResult> transformResults;
  fenceposts.reserve(ctx.edits.size() + 1);
  diffs.reserve(ctx.edits.size());
  transformResults.reserve(ctx.edits.size());
  fenceposts.push_back(initialLines);
  for (auto& e : ctx.edits) {
    fenceposts.push_back(Myers::applyDiffState(e.diffState, fenceposts.back()));
    diffs.push_back(std::move(e.diffState));
    transformResults.push_back(std::move(e.transformResult));
  }

  int numResults = static_cast<int>(results.size());
  CompositionPlan plan{
      .finalGoalPos = goalPos,
      .diffs = std::move(diffs),
      .fenceposts = std::move(fenceposts),
  };
  return {std::move(results), ctx.getStats(numResults),
          std::move(plan),
          ctx.takeExploredStates(),
          std::move(transformResults)};
}

ostream& operator<<(ostream& os, const CompositionResult& cr) {
  os << cr.getStats() << " goalPos=" << cr.getGoalPos() << "\n";

  auto isReplaceCharToken = [](string_view tok) {
    size_t i = 0;
    while (i < tok.size() && isdigit(static_cast<unsigned char>(tok[i]))) i++;
    return i + 2 == tok.size() && tok[i] == 'r';
  };

  const auto& diffs = cr.getDiffs();
  const auto& results = cr.getResults();

  // Print diff legend: all diffs get sequential {n} labels.
  if (!diffs.empty()) {
    os << "Diffs:";
    for (size_t i = 0; i < diffs.size(); i++) {
      os << " {" << i << "}=" << diffs[i];
    }
    os << "\n";
  }

  // Print each result with edit operations replaced by {n} placeholders.
  // diffIdx tracks which diff we're on: advances on
  // - Delete tokens matching pure deletion diffs
  // - r{char} tokens for single-char replacement diffs
  // - TypedText tokens (replacement/insertion payload)
  for (size_t i = 0; i < results.size(); i++) {
    os << "  [" << i << "] ";

    auto parsed = parseSequence(results[i].getSequence().view());
    if (!parsed) {
      // The optimizer can emit sequences whose grammar `parseSequence` does
      // not model. Today the only such case is the visual-selection strategy
      // `v{motion}d` emitted from TransformOptimizer.cpp (see the `Sequence
      // visualSeq("v")` site): `parseSequence` is a two-state machine
      // (normal <-> insert) and has no visual-mode state, no `v/V/<C-v>`
      // entry rule, and no selection-consuming operator rule.
      //
      // This is a scope choice, not a bug. If the parser grows a visual-mode
      // grammar in the future (or the optimizer stops emitting visual-mode
      // strategies), this fallback becomes dead code and should be replaced
      // with `.value()` to restore assert-fast behavior. Until then, print
      // the raw sequence so human-approval output stays readable instead of
      // aborting mid-report.
      os << results[i].getSequence().view() << "\n";
      continue;
    }
    vector<TaggedToken> tokens = *parsed;
    int diffIdx = 0;
    int numDiffs = static_cast<int>(diffs.size());
    for (size_t j = 0; j < tokens.size(); j++) {
      auto kind = tokens[j].kind;

      // Spacing before this token
      if (j > 0) {
        auto prev = tokens[j - 1].kind;
        if (prev == TokenKind::Escape || prev == TokenKind::Delete ||
            kind == TokenKind::Delete ||
            (prev == TokenKind::Change && kind == TokenKind::TypedText)) {
          os << " ";
        }
      }

      if (kind == TokenKind::Delete &&
          diffIdx < numDiffs && diffs[diffIdx].isPureDeletion()) {
        // Pure deletion diff: show command as-is, advance diffIdx
        os << makePrintable(tokens[j].token);
        diffIdx++;
      } else if (kind == TokenKind::Delete &&
                 diffIdx < numDiffs &&
                 !diffs[diffIdx].isPureDeletion() &&
                 diffs[diffIdx].deletedText.size() == 1 &&
                 diffs[diffIdx].insertedText.size() == 1 &&
                 isReplaceCharToken(tokens[j].token)) {
        // Single-char replacement using r{char} does not produce TypedText.
        // Show a placeholder after the token so diff labels stay aligned.
        os << makePrintable(tokens[j].token);
        os << " {" << diffIdx++ << "}";
      } else if (kind == TokenKind::TypedText && diffIdx < numDiffs) {
        // Replacement/insertion: strip leading control chars (<BS>, <Del>)
        // and show them before the {n} placeholder.
        string_view text = tokens[j].token;
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
        os << makePrintable(tokens[j].token);
      }
    }

    os << " " << results[i].getCost() << "\n";
  }

  return os;
}
