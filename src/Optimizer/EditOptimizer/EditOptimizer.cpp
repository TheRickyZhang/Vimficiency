// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include <algorithm>
#include <optional>

#include "EditSearchContext.h"
#include "SuffixCache.h"

#include "Editor/Edit.h"
#include "Editor/NavContext.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "State/RunningEffort.h"


using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

EditResult::EditResult(vector<Result> results, SearchStats stats,
                       const Lines& initialLines, int bufferFirstLine,
                       int bufferFirstCol, Position goalPos)
    : goalPos(goalPos),
      stats(std::move(stats)),
      results_(std::move(results)),
      firstLine_(bufferFirstLine),
      firstCol_(bufferFirstCol) {
  lineBaseIndex_.reserve(initialLines.size());
  int cumSum = 0;
  for (size_t i = 0; i < initialLines.size(); i++) {
    int colOffset = (i == 0) ? firstCol_ : 0;
    lineBaseIndex_.push_back(cumSum - colOffset);
    // Empty lines have 1 cursor position (col 0), matching initStartingPositions
    int positions = initialLines[i].empty() ? 1 : static_cast<int>(initialLines[i].size());
    cumSum += positions;
  }
}

ostream& operator<<(ostream& os, const EditResult& editResult) {
  os << "typeAllResults: ";
  for(size_t i = 0; i < editResult.results_.size(); i++) {
    const auto& res = editResult.results_[i];
    if (res.isValid()) os << res.sequence; else os << "_";

    if(i < editResult.results_.size() - 1) os << " ";
  }
  os << "\n";
  return os;
}

namespace {

// Build collapse sequence to merge multi-line goal state into single line.
// <BS> joins current line with previous, <Del> joins with next.
KeyedSequence buildCollapseSequence(int totalLines, int cursorLine) {
  KeyedSequence ks;
  ks.appendRepeated(KeyedSequence::BS, cursorLine);
  ks.appendRepeated(KeyedSequence::Del, totalLines - 1 - cursorLine);
  return ks;
}

// Convert characterwise delete command to change equivalent.
//
// dw/dW are converted to dwi/dWi (delete + enter insert) rather than cw/cW because
// vim treats cw/cW like ce/cE (doesn't include trailing whitespace). This conversion
// is only reached when dw/dW is the last delete that reaches the goal — and since
// de/dE (WordEdge) is explored before dw/dW (GapEdge) in exploreAllDeletions, the
// de result is already stored when the ranges are identical. So dw reaching the goal
// implies de didn't, meaning dw deleted trailing whitespace that cw would skip.
KeyedSequence deleteToChangeChar(const KeyedSequence& deleteKS) {
  string_view deleteCmd = deleteKS.seq.keys;
  const PhysicalKeys& deleteKeys = deleteKS.keys;

  if (deleteCmd == "D")
    return {"C", {Key::Key_Shift, Key::Key_C}};
  if (deleteCmd == "dw")
    return {"dwi", {Key::Key_D, Key::Key_W, Key::Key_I}};
  if (deleteCmd == "dW")
    return {"dWi", {Key::Key_D, Key::Key_Shift, Key::Key_W, Key::Key_I}};
  if (deleteCmd[0] == 'd') {
    return {string("c") + string(deleteCmd.substr(1)), deleteKeys.asChange()};
  }
  if (deleteCmd == "x")
    return {"s", {Key::Key_S}};
  if (deleteCmd == "X")
    return {"hs", {Key::Key_H, Key::Key_S}};

  // TODO: Simplify this to not include count in deleteCmd
  // Handle counted commands: extract digit prefix
  size_t numEnd = 0;
  while (numEnd < deleteCmd.size() && isdigit(deleteCmd[numEnd])) numEnd++;
  if (numEnd > 0) {
    string_view countStr = deleteCmd.substr(0, numEnd);
    string_view baseCmd = deleteCmd.substr(numEnd);
    int count = stoi(string(countStr));

    // Strip digit keys from the front to get base delete keys
    PhysicalKeys baseDeleteKeys;
    for (size_t i = numEnd; i < deleteKeys.size(); i++) {
      baseDeleteKeys.push_back(*(deleteKeys.begin() + i));
    }

    if (baseCmd == "dw" || baseCmd == "dW") {
      // {n}dw → {n}dwi (count on dw only, not i)
      PhysicalKeys keys = makeCountedKeys(count, baseDeleteKeys);
      keys.push_back(Key::Key_I);
      return {string(deleteCmd) + "i", keys};
    }
    if (baseCmd.size() > 0 && baseCmd[0] == 'd') {
      // General: {n}d{motion} → {n}c{motion}
      PhysicalKeys changeBaseKeys = baseDeleteKeys.asChange();
      return {string(countStr) + "c" + string(baseCmd.substr(1)),
              makeCountedKeys(count, changeBaseKeys)};
    }
    if (baseCmd == "x") {
      return {string(countStr) + "s", makeCountedKeys(count, {Key::Key_S})};
    }
    if (baseCmd == "X") {
      PhysicalKeys keys = makeCountedKeys(count, {Key::Key_H});
      keys.push_back(Key::Key_S);
      return {string(countStr) + "hs", keys};
    }
  }

  assert(false && "deleteToChangeChar: unsupported command");
  return {};
}

// Convert linewise delete command to change equivalent.
KeyedSequence deleteToChangeLine(const KeyedSequence& deleteKS,
                                  string_view lineContent) {
  string_view deleteCmd = deleteKS.seq.keys;
  const PhysicalKeys& deleteKeys = deleteKS.keys;

  // dd is special: cc when no autoindent issue, 0C when autoindent would
  if (deleteCmd == "dd") {
    if constexpr (VimOptions::autoindent()) {
      if (!leadingWhitespace(lineContent).empty()) {
        return {"0C", {Key::Key_0, Key::Key_Shift, Key::Key_C}};
      }
    }
    return {"cc", {Key::Key_C, Key::Key_C}};
  }

  // dj → cj, dk → ck
  if (deleteCmd == "dj" || deleteCmd == "dk") {
    return {string("c") + string(deleteCmd.substr(1)), deleteKeys.asChange()};
  }

  // {n}dd → {n}cc (with autoindent check)
  if (deleteCmd.size() >= 3 && deleteCmd.back() == 'd' &&
      deleteCmd[deleteCmd.size() - 2] == 'd') {
    size_t numEnd = 0;
    while (numEnd < deleteCmd.size() && isdigit(deleteCmd[numEnd])) numEnd++;
    if (numEnd > 0 && deleteCmd.substr(numEnd) == "dd") {
      string countStr(deleteCmd.substr(0, numEnd));
      int count = stoi(countStr);
      if constexpr (VimOptions::autoindent()) {
        if (!leadingWhitespace(lineContent).empty()) {
          // {n}dd with autoindent → {n}0C doesn't exist; use {n}cc with 0 prefix
          // Actually: {n}cc enters change on n lines, which handles autoindent.
          // But the cursor line has leading whitespace so cc would re-indent.
          // Simplest: use countStr + "0C" approach doesn't scale. Just use {n}cc.
          // The autoindent issue with cc is that it re-indents. Fall through to {n}cc.
        }
      }
      static const PhysicalKeys ccKeys = {Key::Key_C, Key::Key_C};
      return {countStr + "cc", makeCountedKeys(count, ccKeys)};
    }
  }

  return {string("c") + string(deleteCmd.substr(1)), deleteKeys.asChange()};
}


// Edits where N{edit} produces the same result as repeating {edit} N times.
// Only single-character operations qualify: for operator+motion combos like de,
// Vim interprets the count on the motion (d4e ≠ dededede), and for linewise
// commands like dd/J, the count changes the scope (4dd = delete 4 lines at once,
// not dd repeated 4 times).
bool isSafeForCountCollapse(string_view edit) {
  return edit == "x" || edit == "X" || edit == "~";
}

// Post-hoc collapse of dot-repeated edits into counted form.
// Scans each result's sequence for a safe edit followed by consecutive '.' tokens.
// If the group count >= minCountRepeat, replaces with {count}{edit} and keeps
// the collapsed version only if it has lower or equal effort.
void collapseCountRepeats(vector<Result>& results, int minCountRepeat,
                          const Config& config) {
  if (minCountRepeat <= 1) return;

  for (auto& result : results) {
    if (!result.isValid()) continue;

    vector<ParsedEdit> edits = Edit::parseEdits(result.sequence.keys);
    if (edits.empty()) continue;

    // Scan for collapsible groups: safe edit + consecutive dots
    bool anyCollapsed = false;
    vector<pair<int, int>> collapseRanges; // (startIdx, count) of groups to collapse

    for (int i = 0; i < static_cast<int>(edits.size()); i++) {
      if (edits[i].hasCount()) continue;
      if (!isSafeForCountCollapse(edits[i].edit)) continue;

      // Count consecutive dots after this edit
      int dotCount = 0;
      int j = i + 1;
      while (j < static_cast<int>(edits.size()) && edits[j].edit == ".") {
        dotCount++;
        j++;
      }

      int totalReps = 1 + dotCount;
      if (totalReps >= minCountRepeat) {
        collapseRanges.push_back({i, totalReps});
        anyCollapsed = true;
        i = j - 1; // skip past the dots
      }
    }

    if (!anyCollapsed) continue;

    // Rebuild sequence with collapsed groups
    string newSeq;
    int editIdx = 0;
    size_t collapseIdx = 0;

    while (editIdx < static_cast<int>(edits.size())) {
      if (collapseIdx < collapseRanges.size() &&
          editIdx == collapseRanges[collapseIdx].first) {
        int count = collapseRanges[collapseIdx].second;
        newSeq += to_string(count);
        newSeq += edits[editIdx].edit;
        editIdx += count; // skip the edit + its dots
        collapseIdx++;
      } else {
        if (edits[editIdx].hasCount()) {
          newSeq += to_string(edits[editIdx].effectiveCount());
        }
        newSeq += edits[editIdx].edit;
        editIdx++;
      }
    }

    double newEffort = getEffort(newSeq, config);
    if (newEffort <= result.keyCost) {
      result.sequence = Sequence(std::move(newSeq));
      result.keyCost = newEffort;
    }
  }
}

} // anonymous namespace

// replacement strategy for same-length transformations
// Returns result for position 0 only (the only position ever consumed)
optional<Result> tryReplacement(string_view deleted, string_view inserted,
                                const Config &config) {
  assert(deleted.size() == inserted.size());
  assert(deleted != inserted);

  // Find all differing positions
  vector<int> diff;
  for (size_t i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) {
      diff.push_back(static_cast<int>(i));
    }
  }

  int firstDiff = diff[0];

  // Build replacement sequence from firstDiff onward
  // Group consecutive diff positions into runs, use R-mode for runs of 2+
  KeyedSequence ks;
  static const KeyedSequence rCmd("r", {Key::Key_R});
  static const KeyedSequence lCmd("l", {Key::Key_L});
  static const KeyedSequence fCmd("f", {Key::Key_F});

  size_t i = 0;
  while (i < diff.size()) {
    int runStart = diff[i];

    // Find consecutive positions (for R-mode)
    size_t j = i;
    while (j + 1 < diff.size() && diff[j + 1] == diff[j] + 1 &&
           inserted[diff[j + 1]] == inserted[diff[j]]) {
      j++;
    }

    int runLength = static_cast<int>(j - i + 1);
    if (runLength == 1) {
      ks += rCmd;
      ks.appendChar(inserted[runStart]);
    } else {
      // Can use {cnt}r if same consecutive inserted
      ks.appendCounted(runLength, rCmd);
      ks.appendChar(inserted[runStart]);
    }

    // Navigate to next run if there is one
    i = j + 1;
    if (i < diff.size()) {
      int prevPos = diff[j];
      int nextPos = diff[i];
      int dist = nextPos - prevPos;

      if (dist <= 2) {
        ks.appendRepeated(lCmd, dist);
      } else {
        // Try f-motion: check if target char appears only once in range
        char findChar = deleted[nextPos]; // char at target position in original
        int occurrences = (count(deleted.begin() + prevPos + 1,
                                 deleted.begin() + nextPos, findChar));
        if (occurrences == 0) {
          ks += fCmd;
          ks.appendChar(findChar);
        } else {
          ks.appendCounted(dist, lCmd);
        }
      }
    }
  }

  // Build result for position 0 only
  // If firstDiff > 0, we need to move right to reach the first change
  KeyedSequence prefix;
  RunningEffort runningEffort;
  if (firstDiff > 0) {
    if (firstDiff <= 2) {
      prefix.appendRepeated(lCmd, firstDiff);
      runningEffort.append(PhysicalKeys(firstDiff, Key::Key_L), config);
    } else {
      prefix.appendCounted(firstDiff, lCmd);
      runningEffort.append(makeCountedKeys(firstDiff, {Key::Key_L}), config);
    }
  }

  // Position cursor at end of inserted text, matching goalPos used by
  // CompositionOptimizer (c{motion} + typed + <Esc> also lands at end).
  // Replacement leaves cursor at diff.back(); if there's a common suffix,
  // append movement to reach the end.
  int lastDiff = diff.back();
  int endPos = static_cast<int>(inserted.size()) - 1;
  if (lastDiff < endPos) {
    int dist = endPos - lastDiff;
    if (dist <= 2) {
      ks.appendRepeated(lCmd, dist);
    } else {
      ks.appendCounted(dist, lCmd);
    }
  }

  double effort = runningEffort.append(ks.keys, config);
  KeyedSequence full;
  full += prefix;
  full += ks;
  return Result(std::move(full.seq), effort);
}

// =============================================================================
// =============================================================================
// optimizePureDeletion - simplified deletion-only optimization
// =============================================================================

EditResult
EditOptimizer::optimizePureDeletion(const Lines &initialLines,
                                    EditBoundary editBoundary,
                                    EditOptimizerParams params,
                                    int bufferFirstLine, int bufferFirstCol,
                                    Position goalPos) {
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(initialLines, editBoundary, params, config);
  ctx.initStartingPositions(initialLines);

  // Local alias for goal checking
  const string preSuf = editBoundary.prefix() + editBoundary.suffix();

  vector<Result> results(ctx.totalPositions);

  // Goal check: strict (single line matching preSuf)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    return lines.size() == 1 && lines[0] == preSuf;
  };

  // Deletion handler
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const KeyedSequence& ks) {
    EditState afterDel = base.afterDeletion(range);
    double hCost = ctx.heuristicCost(afterDel.getLines());
    ctx.exploreWithDot(std::move(afterDel), base, ks, hCost);
  };

  // Linewise handler: goal check before cursor adjustment (goal doesn't need 'k' escape)
  auto exploreLinewise = [&](const EditState &base, int line,
                             const KeyedSequence& ks) {
    EditState afterDel = base.afterLinewiseDeletion(line);
    const Lines &lines = afterDel.getLines();

    if (isGoalReached(lines)) {
      ctx.exploreWithDot(std::move(afterDel), base, ks, 0.0);
      return;
    }

    // For search continuation: adjust cursor if it escaped below edit region.
    // Store base command (e.g. "dd") as lastEdit, not the "ddk" variant
    bool needsKEscape = editBoundary.hasLinesBelow() &&
        line >= static_cast<int>(lines.size());
    double hCost = ctx.heuristicCost(lines);

    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;
    if (isDot) {
      KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
      if (needsKEscape) dotCmd += KeyedSequence::k;
      afterDel.recordSearch(dotCmd.seq.keys, dotCmd.keys, ctx.effortWeight, hCost, config);
    } else {
      KeyedSequence searchCmd = ks;
      if (needsKEscape) searchCmd += KeyedSequence::k;
      afterDel.recordSearch(searchCmd.seq.keys, searchCmd.keys, ctx.effortWeight, hCost, config);
      afterDel.setLastEdit(ks.seq.keys);
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Join handler
  auto exploreJoin = [&](const EditState& base, bool addSpace,
                         const KeyedSequence& ks) {
    EditState afterJn = base.afterJoin(addSpace);
    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, ks, hCost);
  };

  // Counted linewise handler (dj, dk, {n}dd)
  auto exploreCountedLinewise = [&](const EditState& base, LineRange range,
                                     const KeyedSequence& ks) {
    EditState afterDel = base.afterMultiLinewiseDeletion(range);
    const Lines& lines = afterDel.getLines();

    if (isGoalReached(lines)) {
      ctx.exploreWithDot(std::move(afterDel), base, ks, 0.0);
      return;
    }

    // k-escape if cursor escapes below edit region
    bool needsKEscape = editBoundary.hasLinesBelow() &&
        range.firstLine >= static_cast<int>(lines.size());
    double hCost = ctx.heuristicCost(lines);

    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;
    if (isDot) {
      KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
      if (needsKEscape) dotCmd += KeyedSequence::k;
      afterDel.recordSearch(dotCmd.seq.keys, dotCmd.keys, ctx.effortWeight, hCost, config);
    } else {
      KeyedSequence searchCmd = ks;
      if (needsKEscape) searchCmd += KeyedSequence::k;
      afterDel.recordSearch(searchCmd.seq.keys, searchCmd.keys, ctx.effortWeight, hCost, config);
      afterDel.setLastEdit(ks.seq.keys);
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Counted join handler ({n}J, {n}gJ)
  auto exploreCountedJoin = [&](const EditState& base, int count, bool addSpace,
                                 const KeyedSequence& ks) {
    EditState afterJn = base.afterMultiJoin(count, addSpace);
    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, ks, hCost);
  };

  // Main search loop
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    // Check goal at pop time — guaranteed lowest cost
    if (isGoalReached(s.getLines())) {
      int idx = s.getStartIndex();
      if (!results[idx].isValid()) {
        results[idx] = Result(s.getSeq(), s.getEffort());
        ctx.resultsFound++;
        ctx.uniquePositionsCovered++;
      }
      continue;
    }

    // Early stopping: skip if this startIndex already has a result
    if (results[s.getStartIndex()].isValid()) continue;

    // Explore all deletions (characterwise + linewise)
    ctx.exploreAllDeletions(
      s,
      [&](const Range& range, const KeyedSequence& ks) {
        exploreDeletion(s, range, ks);
      },
      [&](int line, const KeyedSequence& ks) {
        exploreLinewise(s, line, ks);
      },
      [&](const Position& newPos, const KeyedSequence& ks) {
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(ks.seq.keys, ks.keys,
                              ctx.effortWeight, ctx.heuristicCost(newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, const KeyedSequence& ks) {
        exploreJoin(s, addSpace, ks);
      }
    );

    // Explore counted operations
    ctx.exploreCountedLineEdits(s,
      [&](LineRange range, const KeyedSequence& ks) {
        exploreCountedLinewise(s, range, ks);
      });
    ctx.exploreCountedJoinCommands(s,
      [&](int count, bool addSpace, const KeyedSequence& ks) {
        exploreCountedJoin(s, count, addSpace, ks);
      });
    ctx.exploreCountedWordEdits(s,
      [&](const Range& range, const KeyedSequence& ks) {
        exploreDeletion(s, range, ks);
      });
  }

  // Try visual mode deletion: v{motion}d from first content position to last
  // This only applies to position 0 (first content position)
  if (ctx.effectiveLines.size() > 1 ||
      static_cast<int>(ctx.effectiveLines[0].size()) > ctx.leftColOffset + ctx.rightColOffset) {
    // First content position
    Position firstPos(0, ctx.leftColOffset);

    // Last content position
    int lastLine = ctx.effectiveLines.lastLine();
    int lastCol = static_cast<int>(ctx.effectiveLines[lastLine].size()) - 1 - ctx.rightColOffset;
    Position lastPos(lastLine, max(0, lastCol));

    // Only try visual if there's actual content to select
    if (lastPos > firstPos || (lastPos.line == firstPos.line && lastPos.col > firstPos.col)) {
      MotionOptimizer motionOpt(config);
      NavContext navCtx;

      // Find best motion from first to last
      // Use params.motionLinePadding* for consistency with other optimizer calls
      // See docs/optimizer/buffer-slicing.md for padding rationale
      auto [motionResults, motionStats] = motionOpt.optimize(
          ctx.effectiveLines,
          firstPos,
          lastPos,
          MotionOptimizerParams{}
              .withLinePaddingAbove(params.motionLinePaddingAbove)
              .withLinePaddingBelow(params.motionLinePaddingBelow)
              .withMinCountRepeat(params.minCountRepeat)
      );

      if (!motionResults.empty() && motionResults[0].isValid()) {
        // Build visual mode sequence: v + motion + d
        Sequence visualSeq("v");
        visualSeq.append(motionResults[0].sequence.keys);
        visualSeq.append("d");

        // Calculate effort: v + motion + d
        RunningEffort effort;
        static const PhysicalKeys vKey = {Key::Key_V};
        static const PhysicalKeys dKey = {Key::Key_D};
        effort.append(vKey, config);
        effort.append(globalTokenizer().tokenize(motionResults[0].sequence.keys), config);
        double totalEffort = effort.append(dKey, config);

        // Compare with existing result[0] and use better one
        if (!results[0].isValid() || totalEffort < results[0].keyCost) {
          results[0] = Result(std::move(visualSeq), totalEffort);
        }
      }
    }
  }

  collapseCountRepeats(results, params.minCountRepeat, config);

  return EditResult(std::move(results), ctx.getStats(), initialLines,
                    bufferFirstLine, bufferFirstCol, goalPos);
}

// =============================================================================
// optimizeEdit - cross-position sharing via suffix caching
// =============================================================================

EditResult
EditOptimizer::optimizeEdit(
    const Lines &initialLines, const Lines &goalLines,
    EditBoundary editBoundary, EditOptimizerParams params,
    int bufferFirstLine, int bufferFirstCol, Position goalPos) {
  assert(initialLines != goalLines);
  assert(!initialLines.empty());

  // Delegate to optimizePureDeletion for pure deletion (goalLines empty)
  if (all_of(goalLines.begin(), goalLines.end(), [](Line l) {
    return l.empty();
  })) {
    return optimizePureDeletion(initialLines, editBoundary, params,
                                bufferFirstLine, bufferFirstCol, goalPos);
  }

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(initialLines, editBoundary, params, config);
  ctx.initStartingPositions(initialLines);

  // Local aliases for goal checking
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  const string preSuf = pre + suf;

  vector<Result> results(ctx.totalPositions);

  // Check if replacement strategy is applicable (same-length, single-line)
  optional<Result> replacementResult;
  if (initialLines.size() == 1 && goalLines.size() == 1 &&
      initialLines[0].size() == goalLines[0].size()) {
    replacementResult = tryReplacement(initialLines[0], goalLines[0], config);
  }

  // Precompute typed content for char-wise goal state
  int sufLeadingSpaces = (goalLines.size() > 1) ? leadingSpaceCount(suf) : 0;
  KeyedSequence typed = buildTypedCommands(goalLines, "", pre, sufLeadingSpaces);

  // Goal check: accepts multi-line (for collapse via <BS>/<Del>)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.size() == 1) return lines[0] == preSuf;
    if (lines[0] != pre) return false;
    if (lines.back() != suf) return false;
    for (size_t i = 1; i < lines.size() - 1; i++) {
      if (!lines[i].empty()) return false;
    }
    return true;
  };

  // ---- Suffix cache structures ----
  SuffixCacheMap suffixCache;

  int cacheHits = 0;
  int cachePopulations = 0;

  // Helper: replay search sequence from seed state, caching suffixes at each
  // intermediate position. Called when a goal is found. Parses the accumulated
  // search sequence into individual edits and replays them forward from the
  // seed state, building suffix KeyedSequences backward from the goal suffix.
  auto replayAndCacheSuffix = [&](int startIndex, const string& searchSeq,
                                   const KeyedSequence& goalSuffix,
                                   const RunningEffort& goalSuffixEffort) {
    cachePopulations++;

    // Parse search sequence into individual edits
    vector<ParsedEdit> edits = Edit::parseEdits(searchSeq);
    int n = static_cast<int>(edits.size());
    if (n == 0) return;

    // The searchSeq string must outlive the ParsedEdits (they hold string_views)
    // — searchSeq is a const ref to the goal state's seq which stays alive.

    // Build full command strings (with count prefix) and tokenize
    vector<string> editStrs(n);
    vector<PhysicalKeys> editKeys(n);
    for (int i = 0; i < n; i++) {
      if (edits[i].hasCount()) {
        editStrs[i] = to_string(edits[i].effectiveCount()) + string(edits[i].edit);
      } else {
        editStrs[i] = string(edits[i].edit);
      }
      editKeys[i] = globalTokenizer().tokenize(editStrs[i]);
    }

    // Build suffix KeyedSequences BACKWARD: suffix[i] = edit[i..n-1] + goalSuffix
    // suffix[n] = goalSuffix (at the goal state)
    // suffix[i] = edit[i] + suffix[i+1]
    vector<KeyedSequence> suffixKs(n + 1);
    vector<RunningEffort> suffixEfforts(n + 1);
    suffixKs[n] = goalSuffix;
    suffixEfforts[n] = goalSuffixEffort;

    for (int i = n - 1; i >= 0; i--) {
      KeyedSequence editKs(editStrs[i], editKeys[i]);
      suffixKs[i] = editKs;
      suffixKs[i] += suffixKs[i + 1];

      RunningEffort editEffort;
      editEffort.append(editKeys[i], config);
      suffixEfforts[i] = RunningEffort::merge(editEffort, suffixEfforts[i + 1]);
    }

    // Replay FORWARD from seed state, caching at each intermediate position
    Lines replayLines = ctx.effectiveLines;
    Position replayPos = ctx.seedPositionFor(startIndex, initialLines);
    Mode replayMode = Mode::Normal;

    // Cache the seed state (suffix[0] = full search + goal suffix)
    size_t replayHash = hashLines(replayLines);
    SuffixKey seedKey(replayHash, static_cast<int>(replayLines.size()), replayPos, replayMode);
    if (suffixCache.find(seedKey) == suffixCache.end()) {
      suffixCache[seedKey] = SuffixValue{suffixKs[0], suffixEfforts[0]};
    }

    // Replay each edit and cache; stop when entering Insert mode
    string lastEditCmd;
    for (int i = 0; i < n; i++) {
      Edit::applyEdit(replayLines, replayPos, replayMode, edits[i], &lastEditCmd);
      if (replayMode == Mode::Insert) break;

      replayHash = hashLines(replayLines);
      SuffixKey sk(replayHash, static_cast<int>(replayLines.size()), replayPos, replayMode);
      if (suffixCache.find(sk) == suffixCache.end()) {
        SuffixValue sv{suffixKs[i + 1], suffixEfforts[i + 1]};

        // If suffix starts with '.', expand the first dot to the explicit command
        // for context-independent caching. Store both variants so we can collapse
        // back to '.' at lookup time when lastEdit matches.
        if (i + 1 < n && edits[i + 1].edit == "." && !lastEditCmd.empty()) {
          PhysicalKeys expandedKeys = globalTokenizer().tokenize(lastEditCmd);
          KeyedSequence expanded(lastEditCmd, expandedKeys);
          expanded += suffixKs[i + 2];

          RunningEffort expandedEffort;
          expandedEffort.append(expanded.keys, config);

          sv.expandedDotCmd = lastEditCmd;
          sv.dotKs = sv.ks;       // original with '.'
          sv.dotEffort = sv.effort;
          sv.ks = expanded;
          sv.effort = expandedEffort;
        }

        suffixCache[sk] = std::move(sv);
      }
    }
  };

  // Helper: build goal suffix for a deletion that reaches goal.
  // Converts delete→change and appends collapse + typed content.
  //
  // The change command (c) uses Insert mode for deleteRange, which preserves
  // empty merged lines that Normal mode (d) would remove. We detect this
  // cheaply from the range rather than re-simulating the deletion.
  // See docs/core/vim-edge-cases.md §2 for the full explanation.
  auto buildGoalSuffix = [&](const KeyedSequence& deleteKS,
                             const Lines& postDelLines, const Position& postDelPos,
                             const Lines& preDelLines, const Range& range) -> KeyedSequence {
    int totalLines = static_cast<int>(postDelLines.size());
    int cursorLine = postDelPos.line;

    // This operates on a Range, not a LineRange, hence we have more specific logic for this!
    // Check if Insert mode would keep an empty merged line that Normal mode
    // removed. Condition: multiline range starting at col 0, where the merged
    // line (prefix of first line + suffix of last line) is empty, and there
    // are still other lines remaining after the merge.
    if (range.first.line != range.last.line && range.first.col == 0) {
      const string& lastLine = preDelLines[range.last.line];
      if (range.last.col >= static_cast<int>(lastLine.size()) - 1) {
        int linesAfterMerge = static_cast<int>(preDelLines.size())
                            - (range.last.line - range.first.line);
        if (linesAfterMerge > 1) {
          // Insert mode keeps the empty line; Normal mode removed it.
          // Insert mode cursor stays on the empty merged line (one
          // line higher than Normal mode clamped position) -> one more <BS>
          totalLines++;
          cursorLine++;
        }
      }
    }

    KeyedSequence goalSuffix = deleteToChangeChar(deleteKS);
    goalSuffix += buildCollapseSequence(totalLines, cursorLine);
    goalSuffix += typed;
    return goalSuffix;
  };

  // Deletion handler: if goal reached, convert delete→change (lookahead).
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const KeyedSequence& ks) {
    EditState afterDel = base.afterDeletion(range);
    const Lines &lines = afterDel.getLines();
    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;

    if (isGoalReached(lines)) {
      // Normal goal path
      {
        KeyedSequence goalSuffix = buildGoalSuffix(ks,
                                                    lines, afterDel.getPos(),
                                                    base.getLines(), range);

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterDel;
        realState.recordSearch(goalSuffix.seq.keys, goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(ks.seq.keys);
        ctx.exploreNewState(std::move(realState));
      }

      // Dot goal path: . + i + collapse + typed (skip replayAndCacheSuffix)
      if (isDot) {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence dotSuffix(".", KeyedSequence::Period.keys);
        dotSuffix += iCmd;
        dotSuffix += buildCollapseSequence(
            static_cast<int>(lines.size()), afterDel.getPos().line);
        dotSuffix += typed;

        EditState dotState = std::move(afterDel);
        dotState.recordSearch(dotSuffix.seq.keys, dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    double hCost = ctx.heuristicCost(lines);
    ctx.exploreWithDot(std::move(afterDel), base, ks, hCost);
  };

  // Linewise handler: goal check before cursor adjustment (goal doesn't need 'k' escape).
  // dd→cc conversion for goal states.
  auto exploreLinewise = [&](const EditState &base, int line,
                             const KeyedSequence& ks) {
    EditState afterDel = base.afterLinewiseDeletion(line);
    const Lines &lines = afterDel.getLines();
    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;

    if (isGoalReached(lines)) {
      // Normal goal path
      {
        int ccLineCount = static_cast<int>(base.getLines().size());

        KeyedSequence goalSuffix = deleteToChangeLine(ks, base.getLines()[line]);
        goalSuffix += buildCollapseSequence(ccLineCount, line);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterDel;
        realState.recordSearch(goalSuffix.seq.keys, goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(ks.seq.keys);
        ctx.exploreNewState(std::move(realState));
      }

      // Dot goal path: . + i + collapse + typed (skip replayAndCacheSuffix)
      if (isDot) {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        int ccLineCount = static_cast<int>(base.getLines().size());
        KeyedSequence dotSuffix(".", KeyedSequence::Period.keys);
        dotSuffix += iCmd;
        dotSuffix += buildCollapseSequence(ccLineCount, line);
        dotSuffix += typed;

        EditState dotState = std::move(afterDel);
        dotState.recordSearch(dotSuffix.seq.keys, dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    // For non-goal: adjust cursor if escaped below edit region
    Position pos = afterDel.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    bool needsKEscape = editBoundary.hasLinesBelow() && lastValidLine >= 0 && pos.line > lastValidLine;
    if (needsKEscape) {
      pos.line = lastValidLine;
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0 :
                 min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      afterDel.setPos(pos);
    }

    double hCost = ctx.heuristicCost(lines);

    if (isDot) {
      KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
      if (needsKEscape) dotCmd += KeyedSequence::k;
      afterDel.recordSearch(dotCmd.seq.keys, dotCmd.keys, ctx.effortWeight, hCost, config);
    } else {
      KeyedSequence searchCmd = ks;
      if (needsKEscape) searchCmd += KeyedSequence::k;
      afterDel.recordSearch(searchCmd.seq.keys, searchCmd.keys, ctx.effortWeight, hCost, config);
      afterDel.setLastEdit(ks.seq.keys);
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Join handler: if goal reached, J + enter insert + collapse + typed.
  auto exploreJoin = [&](const EditState& base, bool addSpace,
                         const KeyedSequence& ks) {
    EditState afterJn = base.afterJoin(addSpace);
    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;

    if (isGoalReached(afterJn.getLines())) {
      // Normal goal path
      {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence goalSuffix = ks;
        goalSuffix += iCmd;
        goalSuffix += buildCollapseSequence(
            static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterJn;
        realState.recordSearch(goalSuffix.seq.keys, goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(ks.seq.keys);
        ctx.exploreNewState(std::move(realState));
      }

      // Dot goal path: . + i + collapse + typed (skip replayAndCacheSuffix)
      if (isDot) {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence dotSuffix(".", KeyedSequence::Period.keys);
        dotSuffix += iCmd;
        dotSuffix += buildCollapseSequence(
            static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
        dotSuffix += typed;

        EditState dotState = std::move(afterJn);
        dotState.recordSearch(dotSuffix.seq.keys, dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, ks, hCost);
  };

  // Counted linewise handler (dj, dk, {n}dd) - with goal check and d→c conversion
  auto exploreCountedLinewise = [&](const EditState& base, LineRange range,
                                     const KeyedSequence& ks) {
    EditState afterDel = base.afterMultiLinewiseDeletion(range);
    const Lines& lines = afterDel.getLines();
    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;
    int lineCount = range.lastLine - range.firstLine + 1;

    if (isGoalReached(lines)) {
      // Normal goal path: d→c conversion
      {
        // cc line count is pre-deletion line count minus deleted lines plus 1
        // (the change command replaces deleted lines with one insert line)
        int ccLineCount = static_cast<int>(base.getLines().size()) - lineCount + 1;

        KeyedSequence goalSuffix = deleteToChangeLine(ks, base.getLines()[range.firstLine]);
        goalSuffix += buildCollapseSequence(ccLineCount, range.firstLine);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterDel;
        realState.recordSearch(goalSuffix.seq.keys, goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(ks.seq.keys);
        ctx.exploreNewState(std::move(realState));
      }

      // Dot goal path
      if (isDot) {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        int ccLineCount = static_cast<int>(base.getLines().size()) - lineCount + 1;
        KeyedSequence dotSuffix(".", KeyedSequence::Period.keys);
        dotSuffix += iCmd;
        dotSuffix += buildCollapseSequence(ccLineCount, range.firstLine);
        dotSuffix += typed;

        EditState dotState = std::move(afterDel);
        dotState.recordSearch(dotSuffix.seq.keys, dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    // Non-goal: k-escape if cursor escapes below edit region
    Position pos = afterDel.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    bool needsKEscape = editBoundary.hasLinesBelow() && lastValidLine >= 0 && pos.line > lastValidLine;
    if (needsKEscape) {
      pos.line = lastValidLine;
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0 :
                 min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      afterDel.setPos(pos);
    }

    double hCost2 = ctx.heuristicCost(lines);

    if (isDot) {
      KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
      if (needsKEscape) dotCmd += KeyedSequence::k;
      afterDel.recordSearch(dotCmd.seq.keys, dotCmd.keys, ctx.effortWeight, hCost2, config);
    } else {
      KeyedSequence searchCmd = ks;
      if (needsKEscape) searchCmd += KeyedSequence::k;
      afterDel.recordSearch(searchCmd.seq.keys, searchCmd.keys, ctx.effortWeight, hCost2, config);
      afterDel.setLastEdit(ks.seq.keys);
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Counted join handler ({n}J, {n}gJ) - with goal check
  auto exploreCountedJoin = [&](const EditState& base, int count, bool addSpace,
                                 const KeyedSequence& ks) {
    EditState afterJn = base.afterMultiJoin(count, addSpace);
    bool isDot = !base.getLastEdit().empty() && base.getLastEdit() == ks.seq.keys;

    if (isGoalReached(afterJn.getLines())) {
      // Normal goal path
      {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence goalSuffix = ks;
        goalSuffix += iCmd;
        goalSuffix += buildCollapseSequence(
            static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterJn;
        realState.recordSearch(goalSuffix.seq.keys, goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(ks.seq.keys);
        ctx.exploreNewState(std::move(realState));
      }

      // Dot goal path
      if (isDot) {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence dotSuffix(".", KeyedSequence::Period.keys);
        dotSuffix += iCmd;
        dotSuffix += buildCollapseSequence(
            static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
        dotSuffix += typed;

        EditState dotState = std::move(afterJn);
        dotState.recordSearch(dotSuffix.seq.keys, dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    double hCost2 = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, ks, hCost2);
  };

  // ---- Main search loop ----
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    // Early stopping: skip if this startIndex already has a result
    if (results[s.getStartIndex()].isValid()) continue;

    // Check goal at pop time — guaranteed lowest cost
    if (isGoalReached(s.getLines())) {
      int idx = s.getStartIndex();
      if (!results[idx].isValid()) {
        results[idx] = Result(s.getSeq(), s.getEffort());
        ctx.resultsFound++;
        ctx.uniquePositionsCovered++;
      }
      continue;
    }

    // Check suffix cache
    SuffixKey sk(s.getLinesHash(), static_cast<int>(s.getLines().size()), s.getPos(), s.getMode());
    auto cacheIt = suffixCache.find(sk);
    if (cacheIt != suffixCache.end()) {
      cacheHits++;
      int idx = s.getStartIndex();
      if (!results[idx].isValid()) {
        const SuffixValue& sv = cacheIt->second;

        // Use dot variant if searcher's lastEdit matches the expanded command
        bool useDot = !sv.expandedDotCmd.empty() && s.getLastEdit() == sv.expandedDotCmd;
        const KeyedSequence& suffix = useDot ? sv.dotKs : sv.ks;
        const RunningEffort& suffixEffort = useDot ? sv.dotEffort : sv.effort;

        string seqStr = s.getSeq() + suffix.seq.keys;
        RunningEffort mergedEffort = RunningEffort::merge(s.getRunningEffort(), suffixEffort);
        double totalEffort = mergedEffort.getEffort(config);

        results[idx] = Result(seqStr, totalEffort);
        ctx.resultsFound++;
        ctx.uniquePositionsCovered++;
      }
      continue;
    }

    // Explore all deletions
    ctx.exploreAllDeletions(
      s,
      [&](const Range& range, const KeyedSequence& ks) {
        exploreDeletion(s, range, ks);
      },
      [&](int line, const KeyedSequence& ks) {
        exploreLinewise(s, line, ks);
      },
      [&](const Position& newPos, const KeyedSequence& ks) {
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(ks.seq.keys, ks.keys,
                              ctx.effortWeight, ctx.heuristicCost(newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, const KeyedSequence& ks) {
        exploreJoin(s, addSpace, ks);
      }
    );

    // Explore counted operations
    ctx.exploreCountedLineEdits(s,
      [&](LineRange range, const KeyedSequence& ks) {
        exploreCountedLinewise(s, range, ks);
      });
    ctx.exploreCountedJoinCommands(s,
      [&](int count, bool addSpace, const KeyedSequence& ks) {
        exploreCountedJoin(s, count, addSpace, ks);
      });
    ctx.exploreCountedWordEdits(s,
      [&](const Range& range, const KeyedSequence& ks) {
        exploreDeletion(s, range, ks);
      });
  }

  // Merge replacement result at position 0 if it's better
  if (replacementResult.has_value()) {
    if (!results[0].isValid() ||
        replacementResult->keyCost < results[0].keyCost) {
      results[0] = *replacementResult;
    }
  }

  collapseCountRepeats(results, params.minCountRepeat, config);

  // Build stats with cache info
  SearchStats stats = ctx.getStats();
  stats.cacheHits = cacheHits;
  stats.cacheEntries = static_cast<int>(suffixCache.size());
  stats.cachePopulations = cachePopulations;

  return EditResult(std::move(results), stats, initialLines,
                    bufferFirstLine, bufferFirstCol, goalPos);
}
