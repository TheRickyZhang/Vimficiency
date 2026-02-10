// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"
#include "EditSearchContext.h"
#include "SuffixCache.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/KeyboardModel.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"

#include "Editor/Edit.h"
#include "Editor/NavContext.h"
#include "Keyboard/MotionToKeys.h"
#include "State/RunningEffort.h"

#include <algorithm>
#include <optional>

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

// Check if buffer is effectively empty (all lines are empty strings)
bool allLinesEmpty(const Lines &lines) {
  if (lines.empty()) return true;
  for (const auto &line : lines) {
    if (!line.empty()) return false;
  }
  return true;
}


// Convert delete command to change equivalent
// Returns the change command KeyedSequence, or empty if no mapping exists
//
// dw/dW are converted to dwi/dWi (delete + enter insert) rather than cw/cW because
// vim treats cw/cW like ce/cE (doesn't include trailing whitespace). This conversion
// is only reached when dw/dW is the last delete that reaches the goal — and since
// de/dE (WordEdge) is explored before dw/dW (GapEdge) in exploreAllDeletions, the
// de result is already stored when the ranges are identical. So dw reaching the goal
// implies de didn't, meaning dw deleted trailing whitespace that cw would skip.
KeyedSequence deleteToChange(string_view deleteCmd,
                              const PhysicalKeys& deleteKeys) {
  if (deleteCmd == "D")
    return {"C", {Key::Key_Shift, Key::Key_C}};
  if (deleteCmd == "dd")
    return {"cc", {Key::Key_C, Key::Key_C}};
  if (deleteCmd == "dw")
    return {"dwi", {Key::Key_D, Key::Key_W, Key::Key_I}};
  if (deleteCmd == "dW")
    return {"dWi", {Key::Key_D, Key::Key_Shift, Key::Key_W, Key::Key_I}};
  if (deleteCmd[0] == 'd') {
    // Generic d{motion} → c{motion}: Key_C + motion keys (skip Key_D)
    assert(deleteKeys.view()[0] == Key::Key_D);
    PhysicalKeys changeKeys = {Key::Key_C};
    for (size_t i = 1; i < deleteKeys.size(); i++) {
      changeKeys.push_back(deleteKeys.view()[i]);
    }
    return {string("c") + string(deleteCmd.substr(1)), std::move(changeKeys)};
  }
  if (deleteCmd == "x")
    return {"s", {Key::Key_S}};
  if (deleteCmd == "X")
    return {"hs", {Key::Key_H, Key::Key_S}};
  assert(false && "deleteToChange not supported");
  return {};
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

  // Goal check for pure deletion: only single-line goals accepted
  // (can't collapse multiple empty lines without insert mode)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.size() != 1) return false;
    return lines[0] == preSuf;
  };

  // Deletion handler: output delete command directly (no change conversion)
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             string_view deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterDeletion(range);
    const Lines &lines = newState.getLines();

    if (isGoalReached(lines)) {
      KeyedSequence goalSuffix(deleteCmd, deleteKeys);

      RunningEffort re = base.getRunningEffort();
      double goalEffort = re.append(goalSuffix.keys, config);
      newState.recordGoalCost(goalSuffix.seq.keys, goalEffort, ctx.effortWeight);
      ctx.pushGoalState(std::move(newState));
      return;
    }

    newState.recordSearch(deleteCmd, deleteKeys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // Linewise handler: record dd directly (pure deletion, no cc conversion)
  auto exploreLinewise = [&](const EditState &base, int line,
                             string_view deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterLinewiseDeletion(line);
    const Lines &lines = newState.getLines();

    // For search continuation: adjust cursor if it escaped below edit region.
    // See optimizeEdit's exploreLinewise for detailed comment.
    KeyedSequence searchCmd(deleteCmd, deleteKeys);
    if (editBoundary.hasLinesBelow() &&
        line >= static_cast<int>(lines.size())) {
      searchCmd += KeyedSequence::k;
    }

    if (isGoalReached(lines)) {
      KeyedSequence goalSuffix(deleteCmd, deleteKeys);

      RunningEffort re = base.getRunningEffort();
      double goalEffort = re.append(goalSuffix.keys, config);
      newState.recordGoalCost(goalSuffix.seq.keys, goalEffort, ctx.effortWeight);
      ctx.pushGoalState(std::move(newState));
      return;
    }

    newState.recordSearch(searchCmd.seq.keys, searchCmd.keys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // Main search loop
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    // Handle goal states popped from PQ
    if (s.isGoal()) {
      int idx = s.getStartIndex();
      bool isNew = !results[idx].isValid();
      if (isNew || s.getEffort() < results[idx].keyCost) {
        results[idx] = Result(s.getSeq(), s.getEffort());
      }
      ctx.resultsFound++;
      if (isNew) ctx.uniquePositionsCovered++;
      continue;
    }

    // Early stopping: skip if this startIndex already has a result
    if (results[s.getStartIndex()].isValid()) continue;

    // Join handler for pure deletion: J/gJ merges lines without adding new content
    auto exploreJoin = [&](const EditState& base, bool addSpace,
                           string_view joinCmd, const PhysicalKeys& joinKeys) {
      EditState newState = base.afterJoin(addSpace);
      const Lines& lines = newState.getLines();

      if (isGoalReached(lines)) {
        KeyedSequence goalSuffix(joinCmd, joinKeys);

        RunningEffort re = base.getRunningEffort();
        double goalEffort = re.append(goalSuffix.keys, config);
        newState.recordGoalCost(goalSuffix.seq.keys, goalEffort, ctx.effortWeight);
        ctx.pushGoalState(std::move(newState));
        return;
      }

      newState.recordSearch(joinCmd, joinKeys,
                            ctx.computePriority(newState.getEffort(), lines), config);
      ctx.exploreNewState(std::move(newState));
    };

    // Explore all deletions (characterwise + linewise) via EditSearchContext
    ctx.exploreAllDeletions(
      s,
      [&](const Range& range, string_view cmd, const PhysicalKeys& keys) {
        exploreDeletion(s, range, cmd, keys);
      },
      [&](int line, string_view cmd, const PhysicalKeys& keys) {
        exploreLinewise(s, line, cmd, keys);
      },
      [&](const Position& newPos, string_view cmd, const PhysicalKeys& keys) {
        // Pure cursor movement - no buffer change, no goal check possible
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(cmd, keys,
                              ctx.computePriority(newState.getEffort(), newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, string_view cmd, const PhysicalKeys& keys) {
        exploreJoin(s, addSpace, cmd, keys);
      }
    );
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
  if (allLinesEmpty(goalLines)) {
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

    // Tokenize each edit to get PhysicalKeys
    vector<PhysicalKeys> editKeys(n);
    for (int i = 0; i < n; i++) {
      editKeys[i] = globalTokenizer().tokenize(edits[i].edit);
    }

    // Build suffix KeyedSequences BACKWARD: suffix[i] = edit[i..n-1] + goalSuffix
    // suffix[n] = goalSuffix (at the goal state)
    // suffix[i] = edit[i] + suffix[i+1]
    vector<KeyedSequence> suffixKs(n + 1);
    vector<RunningEffort> suffixEfforts(n + 1);
    suffixKs[n] = goalSuffix;
    suffixEfforts[n] = goalSuffixEffort;

    for (int i = n - 1; i >= 0; i--) {
      KeyedSequence editKs(edits[i].edit, editKeys[i]);
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
    SuffixKey seedKey(replayLines, replayPos, replayMode);
    if (suffixCache.find(seedKey) == suffixCache.end()) {
      suffixCache[seedKey] = SuffixValue{suffixKs[0], suffixEfforts[0]};
    }

    // Replay each edit and cache intermediate states
    for (int i = 0; i < n; i++) {
      Edit::applyEdit(replayLines, replayPos, replayMode, edits[i]);

      SuffixKey sk(replayLines, replayPos, replayMode);
      if (suffixCache.find(sk) == suffixCache.end()) {
        suffixCache[sk] = SuffixValue{suffixKs[i + 1], suffixEfforts[i + 1]};
      }
    }
  };

  // ---- Deletion handler ----
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             string_view deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterDeletion(range);
    const Lines &lines = newState.getLines();

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (results[idx].isValid()) return;

      KeyedSequence goalSuffix = deleteToChange(deleteCmd, deleteKeys);
      goalSuffix += buildCollapseSequence(
          static_cast<int>(lines.size()), newState.getPos().line);
      goalSuffix += typed;

      RunningEffort re = base.getRunningEffort();
      double goalEffort = re.append(goalSuffix.keys, config);
      newState.recordGoalCost(goalSuffix.seq.keys, goalEffort, ctx.effortWeight);
      results[idx] = Result(newState.getSeq(), newState.getEffort());
      ctx.resultsFound++;
      ctx.uniquePositionsCovered++;

      // Populate suffix cache via replay
      RunningEffort goalSuffixEffort;
      goalSuffixEffort.append(goalSuffix.keys, config);
      replayAndCacheSuffix(newState.getStartIndex(), base.getSeq(),
                           goalSuffix, goalSuffixEffort);
      return;
    }

    newState.recordSearch(deleteCmd, deleteKeys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // ---- Linewise handler ----
  auto exploreLinewise = [&](const EditState &base, int line,
                             string_view deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterLinewiseDeletion(line);
    const Lines &lines = newState.getLines();

    // Adjust cursor if escaped below edit region
    KeyedSequence searchCmd(deleteCmd, deleteKeys);
    Position pos = newState.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    if (editBoundary.hasLinesBelow() && lastValidLine >= 0 && pos.line > lastValidLine) {
      searchCmd += KeyedSequence::k;
      pos.line = lastValidLine;
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0 :
                 min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      newState.setPos(pos);
    }

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (results[idx].isValid()) return;

      static const KeyedSequence ccCmd("cc", {Key::Key_C, Key::Key_C});
      int ccLineCount = static_cast<int>(base.getLines().size());

      // cc with autoindent places cursor after indent. Insert <C-u> to clear
      // autoindent back to col 0 so collapse <BS> presses all go to line joins.
      // Then reuse pre-computed `typed` (which assumes col 0 / empty autoindent).
      KeyedSequence goalSuffix = ccCmd;
      if constexpr (VimOptions::autoindent()) {
        if (!leadingWhitespace(base.getLines()[line]).empty()) {
          goalSuffix += KeyedSequence::CtrlU;
        }
      }
      goalSuffix += buildCollapseSequence(ccLineCount, line);
      goalSuffix += typed;

      RunningEffort re = base.getRunningEffort();
      double goalEffort = re.append(goalSuffix.keys, config);
      newState.recordGoalCost(goalSuffix.seq.keys, goalEffort, ctx.effortWeight);
      results[idx] = Result(newState.getSeq(), newState.getEffort());
      ctx.resultsFound++;
      ctx.uniquePositionsCovered++;

      // Populate suffix cache via replay
      RunningEffort goalSuffixEffort;
      goalSuffixEffort.append(goalSuffix.keys, config);
      replayAndCacheSuffix(newState.getStartIndex(), base.getSeq(),
                           goalSuffix, goalSuffixEffort);
      return;
    }

    newState.recordSearch(searchCmd.seq.keys, searchCmd.keys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // ---- Main search loop ----
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    // Early stopping: skip if this startIndex already has a result
    if (results[s.getStartIndex()].isValid()) continue;

    // Check suffix cache
    SuffixKey sk(s.getLines(), s.getPos(), s.getMode());
    auto cacheIt = suffixCache.find(sk);
    if (cacheIt != suffixCache.end()) {
      cacheHits++;
      int idx = s.getStartIndex();
      if (!results[idx].isValid()) {
        const SuffixValue& sv = cacheIt->second;
        string seqStr = s.getSeq() + sv.ks.seq.keys;
        RunningEffort mergedEffort = RunningEffort::merge(s.getRunningEffort(), sv.effort);
        double totalEffort = mergedEffort.getEffort(config);

        results[idx] = Result(seqStr, totalEffort);
        ctx.resultsFound++;
        ctx.uniquePositionsCovered++;
      }
      continue;  // Skip exploration — suffix provides the result
    }

    // Join handler
    auto exploreJoin = [&](const EditState& base, bool addSpace,
                           string_view joinCmd, const PhysicalKeys& joinKeys) {
      EditState newState = base.afterJoin(addSpace);
      const Lines& lines = newState.getLines();

      if (isGoalReached(lines)) {
        int idx = newState.getStartIndex();
        if (results[idx].isValid()) return;
        // J doesn't enter insert mode; continue search
      }

      newState.recordSearch(joinCmd, joinKeys,
                            ctx.computePriority(newState.getEffort(), lines), config);
      ctx.exploreNewState(std::move(newState));
    };

    // Explore all deletions
    ctx.exploreAllDeletions(
      s,
      [&](const Range& range, string_view cmd, const PhysicalKeys& keys) {
        exploreDeletion(s, range, cmd, keys);
      },
      [&](int line, string_view cmd, const PhysicalKeys& keys) {
        exploreLinewise(s, line, cmd, keys);
      },
      [&](const Position& newPos, string_view cmd, const PhysicalKeys& keys) {
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(cmd, keys,
                              ctx.computePriority(newState.getEffort(), newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, string_view cmd, const PhysicalKeys& keys) {
        exploreJoin(s, addSpace, cmd, keys);
      }
    );
  }

  // Merge replacement result at position 0 if it's better
  if (replacementResult.has_value()) {
    if (!results[0].isValid() ||
        replacementResult->keyCost < results[0].keyCost) {
      results[0] = *replacementResult;
    }
  }

  // Build stats with cache info
  SearchStats stats = ctx.getStats();
  stats.cacheHits = cacheHits;
  stats.cacheEntries = static_cast<int>(suffixCache.size());
  stats.cachePopulations = cachePopulations;

  return EditResult(std::move(results), stats, initialLines,
                    bufferFirstLine, bufferFirstCol, goalPos);
}
