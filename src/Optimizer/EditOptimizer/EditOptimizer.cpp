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
// Takes separate (count, baseKS) instead of merged command.
//
// dw/dW are converted to dwi/dWi (delete + enter insert) rather than cw/cW because
// vim treats cw/cW like ce/cE (doesn't include trailing whitespace). This conversion
// is only reached when dw/dW is the last delete that reaches the goal — and since
// de/dE (WordEdge) is explored before dw/dW (GapEdge) in exploreAllDeletions, the
// de result is already stored when the ranges are identical. So dw reaching the goal
// implies de didn't, meaning dw deleted trailing whitespace that cw would skip.
KeyedSequence deleteToChangeChar(int count, const KeyedSequence& baseKS) {
  string_view baseCmd = baseKS.seq.view();
  KeyedSequence result;

  if (baseCmd == "D") {
    result = {"C", {Key::Key_Shift, Key::Key_C}};
  } else if (baseCmd == "x") {
    result = {"s", {Key::Key_S}};
  } else if (baseCmd == "X") {
    result = {"hs", {Key::Key_H, Key::Key_S}};
  } else if (baseCmd == "dw" || baseCmd == "dW") {
    // {n}dw → {n}dwi: count on delete part, i appended
    if (count > 0) result.appendCounted(count, baseKS);
    else result = baseKS;
    result += KeyedSequence("i", {Key::Key_I});
    return result;
  } else if (baseCmd.size() > 1 && baseCmd[0] == 'd') {
    result = {string("c") + string(baseCmd.substr(1)), baseKS.keys.asChange()};
  } else {
    assert(false && "unsupported command in EditOptimizer!");
    return {};
  }

  if (count > 0) {
    KeyedSequence counted;
    counted.appendCounted(count, result);
    return counted;
  }
  return result;
}

// Convert linewise delete command to change equivalent.
// Takes separate (count, baseKS) instead of merged command.
KeyedSequence deleteToChangeLine(int count, const KeyedSequence& baseKS,
                                  string_view lineContent) {
  string_view baseCmd = baseKS.seq.view();

  if (baseCmd == "dd") {
    if (count == 0) {
      // Uncounted dd: cc (or 0C with autoindent whitespace)
      if constexpr (VimOptions::autoindent()) {
        if (!leadingWhitespace(lineContent).empty()) {
          return {"0C", {Key::Key_0, Key::Key_Shift, Key::Key_C}};
        }
      }
      return {"cc", {Key::Key_C, Key::Key_C}};
    }
    // {n}dd → {n}cc
    static const KeyedSequence ccKS("cc", {Key::Key_C, Key::Key_C});
    KeyedSequence result;
    result.appendCounted(count, ccKS);
    return result;
  }

  // dj → cj, dk → ck, or other: d→c substitution
  KeyedSequence result = {string("c") + string(baseCmd.substr(1)), baseKS.keys.asChange()};
  if (count > 0) {
    KeyedSequence counted;
    counted.appendCounted(count, result);
    return counted;
  }
  return result;
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
                             int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterDeletion(range);
    double hCost = ctx.heuristicCost(afterDel.getLines());
    ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, hCost);
  };

  // Linewise handler: goal check before cursor adjustment (goal doesn't need 'k' escape)
  auto exploreLinewise = [&](const EditState &base, int line,
                             int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterLinewiseDeletion(line);
    const Lines &lines = afterDel.getLines();

    if (isGoalReached(lines)) {
      ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, 0.0);
      return;
    }

    // For search continuation: adjust cursor if it escaped below edit region.
    // Store base command (e.g. "dd") as lastEdit, not the "ddk" variant
    bool needsKEscape = editBoundary.hasLinesBelow() &&
        line >= static_cast<int>(lines.size());
    double hCost = ctx.heuristicCost(lines);

    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();
    if (isDot) {
      KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
      if (needsKEscape) dotCmd += KeyedSequence::k;
      afterDel.recordSearch(dotCmd.seq.view(), dotCmd.keys, ctx.effortWeight, hCost, config);
    } else {
      if (needsKEscape) {
        KeyedSequence searchCmd;
        if (count > 0) searchCmd.appendCounted(count, baseKS);
        else searchCmd = baseKS;
        searchCmd += KeyedSequence::k;
        afterDel.recordSearch(searchCmd.seq.view(), searchCmd.keys, ctx.effortWeight, hCost, config);
      } else {
        afterDel.recordSearch(count, baseKS.seq.view(), effort, ctx.effortWeight, hCost, config);
      }
      afterDel.setLastEdit(count, baseKS.seq.view());
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Join handler
  auto exploreJoin = [&](const EditState& base, bool addSpace,
                         int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterJn = base.afterJoin(addSpace);
    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, hCost);
  };

  // Counted linewise handler (dj, dk, {n}dd)
  auto exploreCountedLinewise = [&](const EditState& base, LineRange range,
                                     int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterMultiLinewiseDeletion(range);
    const Lines& lines = afterDel.getLines();

    if (isGoalReached(lines)) {
      ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, 0.0);
      return;
    }

    // k-escape if cursor escapes below edit region
    bool needsKEscape = editBoundary.hasLinesBelow() &&
        range.firstLine >= static_cast<int>(lines.size());
    double hCost = ctx.heuristicCost(lines);

    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();
    if (isDot) {
      KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
      if (needsKEscape) dotCmd += KeyedSequence::k;
      afterDel.recordSearch(dotCmd.seq.view(), dotCmd.keys, ctx.effortWeight, hCost, config);
    } else {
      if (needsKEscape) {
        KeyedSequence searchCmd;
        if (count > 0) searchCmd.appendCounted(count, baseKS);
        else searchCmd = baseKS;
        searchCmd += KeyedSequence::k;
        afterDel.recordSearch(searchCmd.seq.view(), searchCmd.keys, ctx.effortWeight, hCost, config);
      } else {
        afterDel.recordSearch(count, baseKS.seq.view(), effort, ctx.effortWeight, hCost, config);
      }
      afterDel.setLastEdit(count, baseKS.seq.view());
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Counted join handler ({n}J, {n}gJ)
  auto exploreCountedJoin = [&](const EditState& base, int count, bool addSpace,
                                 const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterJn = base.afterMultiJoin(count, addSpace);
    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, hCost);
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
      [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreDeletion(s, range, count, baseKS, effort);
      },
      [&](int line, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreLinewise(s, line, count, baseKS, effort);
      },
      [&](const Position& newPos, const KeyedSequence& ks, const RunningEffort& effort) {
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(ks.seq.view(), effort, ctx.effortWeight,
                              ctx.heuristicCost(newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreJoin(s, addSpace, count, baseKS, effort);
      }
    );

    // Explore counted operations
    ctx.exploreCountedLineEdits(s,
      [&](LineRange range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreCountedLinewise(s, range, count, baseKS, effort);
      });
    ctx.exploreCountedJoinCommands(s,
      [&](int count, bool addSpace, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreCountedJoin(s, count, addSpace, baseKS, effort);
      });
    ctx.exploreCountedWordEdits(s,
      [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreDeletion(s, range, count, baseKS, effort);
      });
    ctx.exploreCountedCharEdits(s,
      [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreDeletion(s, range, count, baseKS, effort);
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
        visualSeq.append(motionResults[0].sequence.view());
        visualSeq.append("d");

        // Calculate effort: v + motion + d
        RunningEffort effort;
        static const PhysicalKeys vKey = {Key::Key_V};
        static const PhysicalKeys dKey = {Key::Key_D};
        effort.append(vKey, config);
        effort.append(globalTokenizer().tokenize(motionResults[0].sequence.view()), config);
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

  // Converts delete -> change and appends collapse + typed content.
  auto buildGoalSuffix = [&](int count, const KeyedSequence& baseKS,
                             const Lines& postDelLines, const Position& postDelPos,
                             const Lines& preDelLines, const Range& range) -> KeyedSequence {
    int totalLines = static_cast<int>(postDelLines.size());
    int cursorLine = postDelPos.line;

    // It is best to retrospectively adjust here because of our delete -> change model rather than re-simulating the deletion.
    // All conditions here are needed: multiline range fully covering lines and there are still other lines remaining after the merge. Easiest way to check is with visual + delete!
    // Note this is characterwise, NOT linewise. That has different logic
    if (range.spansMultiple() && range.first.col == 0 && range.last.col >= preDelLines[range.last.line].size() - 1) {
      // We didn't delete everything -> adjust for one additional kept new line
      if (preDelLines.size() - range.size() > 0) {
        totalLines++;
        cursorLine++;
      }
    }

    KeyedSequence goalSuffix = deleteToChangeChar(count, baseKS);
    goalSuffix += buildCollapseSequence(totalLines, cursorLine);
    goalSuffix += typed;
    return goalSuffix;
  };

  // Deletion handler: if goal reached, convert delete→change (lookahead).
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterDeletion(range);
    const Lines &lines = afterDel.getLines();
    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();

    if (isGoalReached(lines)) {
      // Normal goal path
      {
        KeyedSequence goalSuffix = buildGoalSuffix(count, baseKS,
                                                    lines, afterDel.getPos(),
                                                    base.getLines(), range);

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterDel;
        realState.recordSearch(goalSuffix.seq.view(), goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(count, baseKS.seq.view());
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
        dotState.recordSearch(dotSuffix.seq.view(), dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    double hCost = ctx.heuristicCost(lines);
    ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, hCost);
  };

  // Linewise handler: goal check before cursor adjustment (goal doesn't need 'k' escape).
  // dd→cc conversion for goal states.
  auto exploreLinewise = [&](const EditState &base, int line,
                             int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterLinewiseDeletion(line);
    const Lines &lines = afterDel.getLines();
    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();

    if (isGoalReached(lines)) {
      // Normal goal path
      {
        int ccLineCount = static_cast<int>(base.getLines().size());

        KeyedSequence goalSuffix = deleteToChangeLine(count, baseKS, base.getLines()[line]);
        goalSuffix += buildCollapseSequence(ccLineCount, line);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterDel;
        realState.recordSearch(goalSuffix.seq.view(), goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(count, baseKS.seq.view());
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
        dotState.recordSearch(dotSuffix.seq.view(), dotSuffix.keys,
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
      afterDel.recordSearch(dotCmd.seq.view(), dotCmd.keys, ctx.effortWeight, hCost, config);
    } else {
      if (needsKEscape) {
        KeyedSequence searchCmd;
        if (count > 0) searchCmd.appendCounted(count, baseKS);
        else searchCmd = baseKS;
        searchCmd += KeyedSequence::k;
        afterDel.recordSearch(searchCmd.seq.view(), searchCmd.keys, ctx.effortWeight, hCost, config);
      } else {
        afterDel.recordSearch(count, baseKS.seq.view(), effort, ctx.effortWeight, hCost, config);
      }
      afterDel.setLastEdit(count, baseKS.seq.view());
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Join handler: if goal reached, J + enter insert + collapse + typed.
  auto exploreJoin = [&](const EditState& base, bool addSpace,
                         int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterJn = base.afterJoin(addSpace);
    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();

    if (isGoalReached(afterJn.getLines())) {
      // Normal goal path
      {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence goalSuffix;
        if (count > 0) goalSuffix.appendCounted(count, baseKS);
        else goalSuffix = baseKS;
        goalSuffix += iCmd;
        goalSuffix += buildCollapseSequence(
            static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterJn;
        realState.recordSearch(goalSuffix.seq.view(), goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(count, baseKS.seq.view());
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
        dotState.recordSearch(dotSuffix.seq.view(), dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, hCost);
  };

  // Counted linewise handler (dj, dk, {n}dd) - with goal check and d→c conversion
  auto exploreCountedLinewise = [&](const EditState& base, LineRange range,
                                     int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterMultiLinewiseDeletion(range);
    const Lines& lines = afterDel.getLines();
    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();
    int lineCount = range.lastLine - range.firstLine + 1;

    if (isGoalReached(lines)) {
      // Normal goal path: d→c conversion
      {
        // cc line count is pre-deletion line count minus deleted lines plus 1
        // (the change command replaces deleted lines with one insert line)
        int ccLineCount = static_cast<int>(base.getLines().size()) - lineCount + 1;

        KeyedSequence goalSuffix = deleteToChangeLine(count, baseKS, base.getLines()[range.firstLine]);
        goalSuffix += buildCollapseSequence(ccLineCount, range.firstLine);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterDel;
        realState.recordSearch(goalSuffix.seq.view(), goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(count, baseKS.seq.view());
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
        dotState.recordSearch(dotSuffix.seq.view(), dotSuffix.keys,
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
      afterDel.recordSearch(dotCmd.seq.view(), dotCmd.keys, ctx.effortWeight, hCost2, config);
    } else {
      if (needsKEscape) {
        KeyedSequence searchCmd;
        if (count > 0) searchCmd.appendCounted(count, baseKS);
        else searchCmd = baseKS;
        searchCmd += KeyedSequence::k;
        afterDel.recordSearch(searchCmd.seq.view(), searchCmd.keys, ctx.effortWeight, hCost2, config);
      } else {
        afterDel.recordSearch(count, baseKS.seq.view(), effort, ctx.effortWeight, hCost2, config);
      }
      afterDel.setLastEdit(count, baseKS.seq.view());
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // Counted join handler ({n}J, {n}gJ) - with goal check
  auto exploreCountedJoin = [&](const EditState& base, int count, bool addSpace,
                                 const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterJn = base.afterMultiJoin(count, addSpace);
    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();

    if (isGoalReached(afterJn.getLines())) {
      // Normal goal path
      {
        static const KeyedSequence iCmd("i", {Key::Key_I});
        KeyedSequence goalSuffix;
        if (count > 0) goalSuffix.appendCounted(count, baseKS);
        else goalSuffix = baseKS;
        goalSuffix += iCmd;
        goalSuffix += buildCollapseSequence(
            static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
        goalSuffix += typed;

        RunningEffort suffixEffort;
        suffixEffort.append(goalSuffix.keys, config);
        replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, suffixEffort);

        EditState realState = afterJn;
        realState.recordSearch(goalSuffix.seq.view(), goalSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        realState.setLastEdit(count, baseKS.seq.view());
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
        dotState.recordSearch(dotSuffix.seq.view(), dotSuffix.keys,
                              ctx.effortWeight, 0.0, config);
        ctx.exploreNewState(std::move(dotState));
      }
      return;
    }

    double hCost2 = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, hCost2);
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

        // Use dot variant if searcher's lastEdit matches the expanded command.
        // The expanded command is a full merged string (e.g., "3de"), so reconstruct for comparison.
        auto matchesCountedCmd = [](string_view full, int cnt, string_view base) {
          if (cnt == 0) return full == base;
          string countStr = to_string(cnt);
          return full.size() == countStr.size() + base.size() &&
                 full.substr(0, countStr.size()) == countStr &&
                 full.substr(countStr.size()) == base;
        };
        bool useDot = !sv.expandedDotCmd.empty() &&
                      matchesCountedCmd(sv.expandedDotCmd, s.getLastEditCount(), s.getLastEditBase());
        const KeyedSequence& suffix = useDot ? sv.dotKs : sv.ks;
        const RunningEffort& suffixEffort = useDot ? sv.dotEffort : sv.effort;

        string seqStr = s.getSeq() + suffix.seq.str();
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
      [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreDeletion(s, range, count, baseKS, effort);
      },
      [&](int line, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreLinewise(s, line, count, baseKS, effort);
      },
      [&](const Position& newPos, const KeyedSequence& ks, const RunningEffort& effort) {
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(ks.seq.view(), effort, ctx.effortWeight,
                              ctx.heuristicCost(newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreJoin(s, addSpace, count, baseKS, effort);
      }
    );

    // Explore counted operations
    ctx.exploreCountedLineEdits(s,
      [&](LineRange range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreCountedLinewise(s, range, count, baseKS, effort);
      });
    ctx.exploreCountedJoinCommands(s,
      [&](int count, bool addSpace, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreCountedJoin(s, count, addSpace, baseKS, effort);
      });
    ctx.exploreCountedWordEdits(s,
      [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreDeletion(s, range, count, baseKS, effort);
      });
    ctx.exploreCountedCharEdits(s,
      [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreDeletion(s, range, count, baseKS, effort);
      });
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
