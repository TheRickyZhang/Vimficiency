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
#include "Optimizer/CountPenalty.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "State/RunningEffort.h"
#include "Utils/Indentation.h"


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
  assert(cursorLine >= 0 && cursorLine < totalLines &&
         "cursorLine must be within [0, totalLines)");
  KeyedSequence ks;
  ks.appendRepeated(KeyedSequence::BS, cursorLine);
  ks.appendRepeated(KeyedSequence::Del, totalLines - 1 - cursorLine);
  return ks;
}

// Convert characterwise delete command to change equivalent.
// Takes separate (count, baseKS) instead of merged command.
//
KeyedSequence deleteToChangeChar(int count, const KeyedSequence& baseKS) {
  string_view baseCmd = baseKS.seq.view();
  KeyedSequence result;

  if (baseCmd == "D") {
    result = KeyedSequence::C;
  } else if (baseCmd == "x") {
    result = KeyedSequence::s;
  } else if (baseCmd == "X") {
    result = KeyedSequence::hs;
  } else if (baseCmd == "dw" || baseCmd == "dW") {
    // dw/dW -> dwi/dWi rather than cw/cW because cw/cW == ce/cE (no trailing whitespace). Since de is explored before dw in exploreAllDeletions, we must retain deleting the trailing whitespace
    // Blake
    result.appendCounted(count, baseKS);
    result += KeyedSequence::i;
    return result;
  } else if (baseCmd.size() > 1 && baseCmd[0] == 'd') {
    result = baseKS.asChange();
  } else {
    assert(false && "unsupported command in EditOptimizer!");
    return {};
  }

  KeyedSequence counted;
  counted.appendCounted(count, result);
  return counted;
}

// Convert linewise delete command to change equivalent.
// Takes separate (count, baseKS) instead of merged command.
KeyedSequence deleteToChangeLine(int count, const KeyedSequence& baseKS,
                                  string_view lineContent) {
  string_view baseCmd = baseKS.seq.view();

  if (baseCmd == "dd") {
    if (count == 0) {
      // Use 0C to remove indent, otherwise cc
      if constexpr (VimOptions::autoindent()) {
        if (!leadingWhitespace(lineContent).empty()) {
          return KeyedSequence::ZeroC;
        }
      }
      return KeyedSequence::cc;
    }
    // {n}dd → {n}cc
    return KeyedSequence(count, KeyedSequence::cc);
  }

  // dj → cj, dk → ck
  return KeyedSequence(count, baseKS.asChange());
}

// Replacement strategy for same-length single-line transformations.
// Returns result for position 0 only, or nullopt if effort exceeds maxEffort.
optional<Result> tryReplacement(string_view deleted, string_view inserted,
                                const Config& config, double maxEffort) {
  assert(deleted.size() == inserted.size());
  assert(deleted != inserted);

  vector<int> diff;
  for (size_t i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) diff.push_back(static_cast<int>(i));
  }

  KeyedSequence ks;

  auto appendNav = [&](int dist) {
    if (dist <= 2) ks.appendRepeated(KeyedSequence::l, dist);
    else ks.appendCounted(dist, KeyedSequence::l);
  };

  // Navigate to first diff
  if (diff[0] > 0) appendNav(diff[0]);

  // Build replacement sequence: group consecutive same-char diffs for counted r
  size_t i = 0;
  while (i < diff.size()) {
    size_t j = i;
    while (j + 1 < diff.size() && diff[j + 1] == diff[j] + 1 &&
           inserted[diff[j + 1]] == inserted[diff[j]]) {
      j++;
    }

    int runLength = static_cast<int>(j - i + 1);
    if (runLength > 1) ks.appendCounted(runLength, KeyedSequence::r);
    else ks += KeyedSequence::r;
    ks.appendChar(inserted[diff[i]]);

    // Navigate to next run
    i = j + 1;
    if (i < diff.size()) {
      int dist = diff[i] - diff[j];
      if (dist <= 2) {
        ks.appendRepeated(KeyedSequence::l, dist);
      } else {
        char findChar = deleted[diff[i]];
        int occurrences = count(deleted.begin() + diff[j] + 1,
                                deleted.begin() + diff[i], findChar);
        if (occurrences == 0) {
          ks += KeyedSequence::f;
          ks.appendChar(findChar);
        } else {
          ks.appendCounted(dist, KeyedSequence::l);
        }
      }
    }
  }

  // Position cursor at end of inserted text
  int lastDiff = diff.back();
  int endPos = static_cast<int>(inserted.size()) - 1;
  if (lastDiff < endPos) appendNav(endPos - lastDiff);

  RunningEffort effort;
  double totalEffort = effort.append(ks.keys, config);
  if (totalEffort > maxEffort) return nullopt;

  return Result(std::move(ks.seq), totalEffort);
}

template<CountClass C>
RunningEffort mergeGoalSuffixEffortWithPenalty(const KeyedSequence& prefix,
                                               const RunningEffort& typedSuffixEffort,
                                               int count, int span,
                                               const Config& config) {
  RunningEffort prefixEffort;
  prefixEffort.append(prefix.keys, config);
  if (count > 1) {
    CountPenaltyInput in{count, span};
    double penalty = runtimeCountPenalty<C>(in);
    if (penalty > 0.0) {
      prefixEffort.addPenalty(penalty);
    }
  }
  return RunningEffort::merge(prefixEffort, typedSuffixEffort);
}

RunningEffort mergeGoalSuffixEffortWithDeletionPenalty(const KeyedSequence& prefix,
                                                       const RunningEffort& typedSuffixEffort,
                                                       int count, int span,
                                                       const KeyedSequence& baseKS,
                                                       const Config& config) {
  RunningEffort prefixEffort;
  prefixEffort.append(prefix.keys, config);

  if (count > 1) {
    CountPenaltyInput in{count, span};
    string_view baseCmd = baseKS.seq.view();
    bool matched = true;
    if (baseCmd == "x") {
      prefixEffort.addPenalty(runtimeCountPenalty<CountClass::EditChar>(in));
    } else if (baseCmd == "de" || baseCmd == "dw" ||
               baseCmd == "db" || baseCmd == "dge") {
      prefixEffort.addPenalty(runtimeCountPenalty<CountClass::EditWord>(in));
    } else if (baseCmd == "dE" || baseCmd == "dW" ||
               baseCmd == "dB" || baseCmd == "dgE") {
      prefixEffort.addPenalty(runtimeCountPenalty<CountClass::EditWORD>(in));
    } else if (baseCmd == "d}" || baseCmd == "d{") {
      prefixEffort.addPenalty(runtimeCountPenalty<CountClass::EditParagraph>(in));
    } else if (baseCmd == "d)" || baseCmd == "d(") {
      prefixEffort.addPenalty(runtimeCountPenalty<CountClass::EditSentence>(in));
    } else if (baseCmd == "dd" || baseCmd == "dj" || baseCmd == "dk") {
      prefixEffort.addPenalty(runtimeCountPenalty<CountClass::EditLine>(in));
    } else {
      matched = false;
    }
    assert(matched && "Unsupported counted deletion command for count penalty");
  }

  return RunningEffort::merge(prefixEffort, typedSuffixEffort);
}

template<bool PureDeletion>
struct ModePolicy;

template<>
struct ModePolicy<true> {
  EditSearchContext& ctx;
  const Config& config;
  const string& preSuf;

  ModePolicy(EditSearchContext& ctx, const Config& config,
             const EditBoundary&, const Lines&, const Lines&,
             const string&, const string&, const string& preSuf)
      : ctx(ctx), config(config), preSuf(preSuf) {}

  bool isGoalReached(const Lines& lines) const {
    return lines.size() == 1 && lines[0] == preSuf;
  }

  bool tryUseSuffixCache(const EditState&, vector<Result>&) { return false; }

  void onDeletionGoal(EditState& afterDel, const EditState& base, const Range&,
                      int count, const KeyedSequence& baseKS,
                      const RunningEffort& effort) {
    ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, 0.0);
  }

  void onLinewiseGoal(EditState& afterDel, const EditState& base, int,
                      int count, const KeyedSequence& baseKS,
                      const RunningEffort& effort) {
    ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, 0.0);
  }

  void onJoinGoal(EditState& afterJn, const EditState& base,
                  int count, const KeyedSequence& baseKS,
                  const RunningEffort& effort) {
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, 0.0);
  }

  void onCountedLinewiseGoal(EditState& afterDel, const EditState& base,
                             LineRange, int count, const KeyedSequence& baseKS,
                             const RunningEffort& effort) {
    ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, 0.0);
  }

  void onCountedJoinGoal(EditState& afterJn, const EditState& base,
                         int count, const KeyedSequence& baseKS,
                         const RunningEffort& effort) {
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, 0.0);
  }

  EditResult finalize(vector<Result>&& results, const Lines& initialLines,
                      const Lines&, const EditOptimizerParams& params,
                      int bufferFirstLine, int bufferFirstCol, Position goalPos) {
    // Try visual mode deletion: v{motion}d from first content position to last
    if (ctx.effectiveLines.size() > 1 ||
        static_cast<int>(ctx.effectiveLines[0].size()) > ctx.leftColOffset + ctx.rightColOffset) {
      Position firstPos(0, ctx.leftColOffset);

      int lastLine = ctx.effectiveLines.lastLine();
      int lastCol = static_cast<int>(ctx.effectiveLines[lastLine].size()) - 1 - ctx.rightColOffset;
      Position lastPos(lastLine, max(0, lastCol));

      if (lastPos > firstPos || (lastPos.line == firstPos.line && lastPos.col > firstPos.col)) {
        MotionOptimizer motionOpt(config);

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
          Sequence visualSeq("v");
          visualSeq.append(motionResults[0].sequence.view());
          visualSeq.append("d");

          RunningEffort effort;
          static const PhysicalKeys vKey = {Key::Key_V};
          static const PhysicalKeys dKey = {Key::Key_D};
          effort.append(vKey, config);
          effort.append(globalTokenizer().tokenize(motionResults[0].sequence.view()), config);
          double totalEffort = effort.append(dKey, config);

          if (!results[0].isValid() || totalEffort < results[0].keyCost) {
            results[0] = Result(std::move(visualSeq), totalEffort);
          }
        }
      }
    }

    return EditResult(std::move(results), ctx.getStats(), initialLines,
                      bufferFirstLine, bufferFirstCol, goalPos);
  }
};

template<>
struct ModePolicy<false> {
  EditSearchContext& ctx;
  const Config& config;
  const EditBoundary& editBoundary;
  const Lines& initialLines;
  const Lines& goalLines;
  const string& pre;
  const string& suf;
  const string& preSuf;

  KeyedSequence typed;
  RunningEffort typedEffort;
  SuffixCacheMap suffixCache;
  int cacheHits = 0;
  int cachePopulations = 0;

  int goalFirstIndentLen = 0;
  KeyedSequence typedAfterIndent;
  RunningEffort afterIndentEffort;

  ModePolicy(EditSearchContext& ctx, const Config& config,
             const EditBoundary& editBoundary, const Lines& initialLines,
             const Lines& goalLines, const string& pre,
             const string& suf, const string& preSuf)
      : ctx(ctx),
        config(config),
        editBoundary(editBoundary),
        initialLines(initialLines),
        goalLines(goalLines),
        pre(pre),
        suf(suf),
        preSuf(preSuf) {
    typed = buildTypedCommands(goalLines, "", pre, suf);
    typedEffort.append(typed.keys, config);

    if constexpr (VimOptions::autoindent()) {
      goalFirstIndentLen = leadingSpaceCount(goalLines[0]);
      if (goalFirstIndentLen > 0) {
        // typed starts with goalFirstIndentLen literal spaces — slice to get
        // the version that assumes autoindent provides them.
        typedAfterIndent = KeyedSequence(
            typed.seq.view().substr(goalFirstIndentLen),
            PhysicalKeys(typed.keys.view().subspan(goalFirstIndentLen)));
        afterIndentEffort.append(typedAfterIndent.keys, config);
      }
    }
  }

  bool isGoalReached(const Lines& lines) const {
    if (lines.size() == 1) return lines[0] == preSuf;

    // Multi-line acceptance for collapse via <BS>/<Del>
    if (lines[0] != pre) return false;
    if (lines.back() != suf) return false;
    for (size_t i = 1; i < lines.size() - 1; i++) {
      if (!lines[i].empty()) return false;
    }
    return true;
  }

  bool isDotRepeat(const EditState& base, int count, const KeyedSequence& baseKS) const {
    return base.hasLastEdit() &&
           base.getLastEditCount() == count &&
           base.getLastEditBase() == baseKS.seq.view();
  }

  void replayAndCacheSuffix(int startIndex, const string& searchSeq,
                            const KeyedSequence& goalSuffix,
                            const RunningEffort& goalSuffixEffort) {
    cachePopulations++;

    vector<ParsedEdit> edits = Edit::parseEdits(searchSeq);
    int n = static_cast<int>(edits.size());
    if (n == 0) return;

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

    Lines replayLines = ctx.effectiveLines;
    Position replayPos = ctx.seedPositionFor(startIndex, initialLines);
    Mode replayMode = Mode::Normal;

    size_t replayHash = hashLines(replayLines);
    SuffixKey seedKey(replayHash, static_cast<int>(replayLines.size()), replayPos, replayMode);
    if (suffixCache.find(seedKey) == suffixCache.end()) {
      suffixCache[seedKey] = SuffixValue{suffixKs[0], suffixEfforts[0]};
    }

    string lastEditCmd;
    for (int i = 0; i < n; i++) {
      Edit::applyEdit(replayLines, replayPos, replayMode, edits[i], &lastEditCmd,
                      editBoundary.hasLinesBelow());
      if (replayMode == Mode::Insert) break;

      replayHash = hashLines(replayLines);
      SuffixKey sk(replayHash, static_cast<int>(replayLines.size()), replayPos, replayMode);
      if (suffixCache.find(sk) == suffixCache.end()) {
        SuffixValue sv{suffixKs[i + 1], suffixEfforts[i + 1]};

        if (i + 1 < n && edits[i + 1].edit == "." && !lastEditCmd.empty()) {
          PhysicalKeys expandedKeys = globalTokenizer().tokenize(lastEditCmd);
          KeyedSequence expanded(lastEditCmd, expandedKeys);
          expanded += suffixKs[i + 2];

          RunningEffort expandedEffort;
          expandedEffort.append(expanded.keys, config);

          sv.expandedDotCmd = lastEditCmd;
          sv.dotKs = sv.ks;
          sv.dotEffort = sv.effort;
          sv.ks = expanded;
          sv.effort = expandedEffort;
        }

        suffixCache[sk] = std::move(sv);
      }
    }
  }

  KeyedSequence buildChangePrefix(int count, const KeyedSequence& baseKS,
                                  const Lines& postDelLines, const Position& postDelPos,
                                  const Lines& preDelLines, const Range& range) const {
    int totalLines = static_cast<int>(postDelLines.size());
    int cursorLine = postDelPos.line;

    if (range.spansMultiple() && range.first.col == 0 &&
        range.last.col >= preDelLines[range.last.line].size() - 1) {
      if (preDelLines.size() - range.size() > 0) {
        totalLines++;
        cursorLine++;
      }
    }

    KeyedSequence prefix = deleteToChangeChar(count, baseKS);
    prefix += buildCollapseSequence(totalLines, cursorLine);
    return prefix;
  }

  // Emit goal via d→c conversion + suffix cache + dot goal path.
  void emitEditGoal(EditState& afterState, const EditState& base,
                    int count, const KeyedSequence& baseKS,
                    const KeyedSequence& goalSuffix,
                    const RunningEffort& goalSuffixEffort,
                    bool isDot) {
    // Normal goal path
    {
      replayAndCacheSuffix(base.getStartIndex(), base.getSeq(), goalSuffix, goalSuffixEffort);

      EditState realState = afterState;
      realState.recordSearch(goalSuffix.seq.view(), goalSuffixEffort,
                             ctx.effortWeight, 0.0, config);
      realState.setLastEdit(count, baseKS.seq.view());
      ctx.exploreNewState(std::move(realState));
    }

    // Dot goal path: . + i + collapse + typed (skip replayAndCacheSuffix).
    // The dot repeats the deletion, then 'i' enters insert mode without autoindent,
    // so no autoindent clearing is needed here.
    if (isDot) {
      const auto& iCmd = KeyedSequence::i;
      KeyedSequence dotSuffix(".", KeyedSequence::Period.keys);
      dotSuffix += iCmd;
      KeyedSequence collapse = buildCollapseSequence(
          static_cast<int>(afterState.getLines().size()), afterState.getPos().line);
      dotSuffix += collapse;

      // O(1) effort: compute prefix effort, merge with pre-computed typedEffort
      RunningEffort dotPrefixEffort;
      dotPrefixEffort.append(dotSuffix.keys, config);
      RunningEffort dotSuffixEffort = RunningEffort::merge(dotPrefixEffort, typedEffort);

      dotSuffix += typed;

      EditState dotState = std::move(afterState);
      dotState.recordSearch(dotSuffix.seq.view(), dotSuffixEffort,
                            ctx.effortWeight, 0.0, config);
      ctx.exploreNewState(std::move(dotState));
    }
  }

  void onDeletionGoal(EditState& afterDel, const EditState& base, const Range& range,
                      int count, const KeyedSequence& baseKS, const RunningEffort&) {
    bool isDot = isDotRepeat(base, count, baseKS);
    KeyedSequence changePrefix = buildChangePrefix(count, baseKS,
                                                   afterDel.getLines(), afterDel.getPos(),
                                                   base.getLines(), range);
    RunningEffort goalSuffixEffort = mergeGoalSuffixEffortWithDeletionPenalty(
        changePrefix, typedEffort, count, count, baseKS, config);
    KeyedSequence goalSuffix = changePrefix;
    goalSuffix += typed;
    emitEditGoal(afterDel, base, count, baseKS, goalSuffix, goalSuffixEffort, isDot);
  }

  void onLinewiseGoal(EditState& afterDel, const EditState& base, int line,
                      int count, const KeyedSequence& baseKS,
                      const RunningEffort&) {
    bool isDot = isDotRepeat(base, count, baseKS);
    int ccLineCount = static_cast<int>(base.getLines().size());
    KeyedSequence changeCmd = deleteToChangeLine(count, baseKS, base.getLines()[line]);
    bool isLinewise = false;
    if constexpr (VimOptions::autoindent()) {
      isLinewise = (changeCmd.seq.view() != "0C");
    }
    int autoindentLen = isLinewise ? leadingSpaceCount(base.getLines()[line]) : 0;
    bool needsCollapse = ccLineCount > 1;

    KeyedSequence changePrefix = changeCmd;
    bool useAfterIndent = false;
    // Collapse BS×cursorLine + Del×rest. Only BS interacts with autoindent.
    bool bsInCollapse = needsCollapse && line > 0;
    if (isLinewise && !bsInCollapse) {
      // Safe: no BS in collapse. Adjust autoindent to goal indent.
      changePrefix += computeIndentAdjustment(autoindentLen, goalFirstIndentLen);
      changePrefix += buildCollapseSequence(ccLineCount, line);
      useAfterIndent = goalFirstIndentLen > 0;
    } else if (isLinewise && bsInCollapse) {
      // BS in collapse: account for autoindent in BS count.
      // bsCountForIndent(x, 0, sw) always succeeds (0 is always a sw boundary).
      int bsClear = autoindentLen > 0 ? bsCountForIndent(autoindentLen, 0) : 0;
      changePrefix.appendRepeated(KeyedSequence::BS, bsClear + line);
      changePrefix.appendRepeated(KeyedSequence::Del, ccLineCount - 1 - line);
    } else {
      changePrefix += buildCollapseSequence(ccLineCount, line);
    }

    const auto& suffixTyped = useAfterIndent ? typedAfterIndent : typed;
    const auto& suffixEffort = useAfterIndent ? afterIndentEffort : typedEffort;
    RunningEffort goalSuffixEffort = mergeGoalSuffixEffortWithPenalty<CountClass::EditLine>(
        changePrefix, suffixEffort, count, count, config);
    KeyedSequence goalSuffix = changePrefix;
    goalSuffix += suffixTyped;
    emitEditGoal(afterDel, base, count, baseKS, goalSuffix, goalSuffixEffort, isDot);
  }

  void onJoinGoal(EditState& afterJn, const EditState& base,
                  int count, const KeyedSequence& baseKS,
                  const RunningEffort&) {
    bool isDot = isDotRepeat(base, count, baseKS);
    const auto& iCmd = KeyedSequence::i;
    KeyedSequence changePrefix;
    changePrefix.appendCounted(count, baseKS);
    changePrefix += iCmd;
    changePrefix += buildCollapseSequence(
        static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
    RunningEffort goalSuffixEffort = mergeGoalSuffixEffortWithPenalty<CountClass::Join>(
        changePrefix, typedEffort, count, count, config);
    KeyedSequence goalSuffix = changePrefix;
    goalSuffix += typed;
    emitEditGoal(afterJn, base, count, baseKS, goalSuffix, goalSuffixEffort, isDot);
  }

  void onCountedLinewiseGoal(EditState& afterDel, const EditState& base,
                             LineRange range, int count, const KeyedSequence& baseKS,
                             const RunningEffort&) {
    bool isDot = isDotRepeat(base, count, baseKS);
    int lineCount = range.lastLine - range.firstLine + 1;
    int ccLineCount = static_cast<int>(base.getLines().size()) - lineCount + 1;
    KeyedSequence changeCmd = deleteToChangeLine(count, baseKS, base.getLines()[range.firstLine]);
    // Counted linewise changes ({n}cc, cj, ck) are always linewise — autoindent applies
    int autoindentLen = 0;
    if constexpr (VimOptions::autoindent()) {
      autoindentLen = leadingSpaceCount(base.getLines()[range.firstLine]);
    }
    bool needsCollapse = ccLineCount > 1;

    KeyedSequence changePrefix = changeCmd;
    bool useAfterIndent = false;
    // Collapse BS×cursorLine + Del×rest. Only BS interacts with autoindent.
    bool bsInCollapse = needsCollapse && range.firstLine > 0;
    if constexpr (VimOptions::autoindent()) {
      if (!bsInCollapse) {
        // Safe: no BS in collapse. Adjust autoindent to goal indent.
        changePrefix += computeIndentAdjustment(autoindentLen, goalFirstIndentLen);
        changePrefix += buildCollapseSequence(ccLineCount, range.firstLine);
        useAfterIndent = goalFirstIndentLen > 0;
      } else {
        // BS in collapse: account for autoindent in BS count.
        int bsClear = autoindentLen > 0 ? bsCountForIndent(autoindentLen, 0) : 0;
        changePrefix.appendRepeated(KeyedSequence::BS, bsClear + range.firstLine);
        changePrefix.appendRepeated(KeyedSequence::Del, ccLineCount - 1 - range.firstLine);
      }
    } else {
      changePrefix += buildCollapseSequence(ccLineCount, range.firstLine);
    }

    const auto& suffixTyped = useAfterIndent ? typedAfterIndent : typed;
    const auto& suffixEffort = useAfterIndent ? afterIndentEffort : typedEffort;
    RunningEffort goalSuffixEffort = mergeGoalSuffixEffortWithPenalty<CountClass::EditLine>(
        changePrefix, suffixEffort, count, count, config);
    KeyedSequence goalSuffix = changePrefix;
    goalSuffix += suffixTyped;
    emitEditGoal(afterDel, base, count, baseKS, goalSuffix, goalSuffixEffort, isDot);
  }

  void onCountedJoinGoal(EditState& afterJn, const EditState& base,
                         int count, const KeyedSequence& baseKS,
                         const RunningEffort&) {
    bool isDot = isDotRepeat(base, count, baseKS);
    const auto& iCmd = KeyedSequence::i;
    KeyedSequence changePrefix;
    changePrefix.appendCounted(count, baseKS);
    changePrefix += iCmd;
    changePrefix += buildCollapseSequence(
        static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
    RunningEffort goalSuffixEffort = mergeGoalSuffixEffortWithPenalty<CountClass::Join>(
        changePrefix, typedEffort, count, count, config);
    KeyedSequence goalSuffix = changePrefix;
    goalSuffix += typed;
    emitEditGoal(afterJn, base, count, baseKS, goalSuffix, goalSuffixEffort, isDot);
  }

  bool tryUseSuffixCache(const EditState& s, vector<Result>& results) {
    SuffixKey sk(s.getLinesHash(), static_cast<int>(s.getLines().size()), s.getPos(), s.getMode());
    auto cacheIt = suffixCache.find(sk);
    if (cacheIt == suffixCache.end()) return false;

    cacheHits++;
    int idx = s.getStartIndex();
    if (!results[idx].isValid()) {
      const SuffixValue& sv = cacheIt->second;

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
    return true;
  }

  EditResult finalize(vector<Result>&& results, const Lines& initialLines,
                      const Lines& goalLines, const EditOptimizerParams&,
                      int bufferFirstLine, int bufferFirstCol, Position goalPos) {
    // Try replacement strategy (same-length, single-line) with A* budget
    if (results[0].isValid() &&
        initialLines.size() == 1 && goalLines.size() == 1 &&
        initialLines[0].size() == goalLines[0].size()) {
      auto replacementResult = tryReplacement(initialLines[0], goalLines[0],
                                              config, results[0].keyCost);
      if (replacementResult.has_value()) {
        results[0] = *replacementResult;
      }
    }

    SearchStats stats = ctx.getStats();
    stats.cacheHits = cacheHits;
    stats.cacheEntries = static_cast<int>(suffixCache.size());
    stats.cachePopulations = cachePopulations;

    return EditResult(std::move(results), stats, initialLines,
                      bufferFirstLine, bufferFirstCol, goalPos);
  }
};

} // anonymous namespace

// =============================================================================
// =============================================================================
// optimizeImpl - unified template for both pure deletion and full edit
// =============================================================================

template<bool PureDeletion>
EditResult EditOptimizer::optimizeImpl(const Lines &initialLines, const Lines &goalLines,
                            EditBoundary editBoundary, EditOptimizerParams params,
                            int bufferFirstLine, int bufferFirstCol,
                            Position goalPos) {
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(initialLines, editBoundary, params, config);
  ctx.initStartingPositions(initialLines);

  // Local aliases for goal checking
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  const string preSuf = pre + suf;

  vector<Result> results(ctx.totalPositions);
  ModePolicy<PureDeletion> mode(ctx, config, editBoundary, initialLines, goalLines,
                                pre, suf, preSuf);

  // Goal check
  auto isGoalReached = [&](const Lines &lines) -> bool {
    return mode.isGoalReached(lines);
  };

  // Non-goal linewise handler (k-escape + dot).
  // Used by both exploreLinewise and exploreCountedLinewise.
  auto exploreLinewiseNonGoal = [&](EditState& afterDel, const EditState& base,
                                     bool needsKEscape, int count,
                                     const KeyedSequence& baseKS, const RunningEffort& effort,
                                     double hCost) {
    // Cursor already clamped by caller (exploreLinewise/exploreCountedLinewise).
    // needsKEscape only controls whether 'k' is appended to the command sequence.

    bool isDot = base.hasLastEdit() &&
                 base.getLastEditCount() == count &&
                 base.getLastEditBase() == baseKS.seq.view();
    if (isDot) {
      if (needsKEscape) {
        KeyedSequence dotCmd(".", KeyedSequence::Period.keys);
        dotCmd += KeyedSequence::k;
        RunningEffort dotEffort = ctx.effortFor(KeyedSequence::Period);
        dotEffort.appendFrom(ctx.effortFor(KeyedSequence::k), config);
        afterDel.recordSearch(dotCmd.seq.view(), dotEffort, ctx.effortWeight, hCost, config);
      } else {
        afterDel.recordSearch(".", ctx.effortFor(KeyedSequence::Period), ctx.effortWeight, hCost, config);
      }
    } else {
      if (needsKEscape) {
        KeyedSequence searchCmd;
        searchCmd.appendCounted(count, baseKS);
        searchCmd += KeyedSequence::k;
        RunningEffort fullEffort = effort;
        fullEffort.appendFrom(ctx.effortFor(KeyedSequence::k), config);
        afterDel.recordSearch(searchCmd.seq.view(), fullEffort, ctx.effortWeight, hCost, config);
      } else {
        afterDel.recordSearch(count, baseKS.seq.view(), effort, ctx.effortWeight, hCost, config);
      }
      afterDel.setLastEdit(count, baseKS.seq.view());
    }
    ctx.exploreNewState(std::move(afterDel));
  };

  // ---- Handlers ----

  // Deletion handler
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterDel = base.afterDeletion(range);
    const Lines &lines = afterDel.getLines();

    if (isGoalReached(lines)) {
      mode.onDeletionGoal(afterDel, base, range, count, baseKS, effort);
      return;
    }

    double hCost = ctx.heuristicCost(lines);
    ctx.exploreWithDot(std::move(afterDel), base, count, baseKS, effort, hCost);
  };

  // Linewise handler
  auto exploreLinewise = [&](const EditState &base, int line,
                             int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    bool hasLinesBelow = editBoundary.hasLinesBelow();
    EditState afterDel = base.afterLinewiseDeletion(line, hasLinesBelow);
    const Lines &lines = afterDel.getLines();

    // Detect and resolve past-end cursor (dd from last effective line with hasLinesBelow).
    // Must happen before goal check so cursor is always valid at goal time.
    bool needsKEscape = afterDel.getPos().line >= static_cast<int>(lines.size());
    if (needsKEscape) {
      Position pos = afterDel.getPos();
      pos.line = static_cast<int>(lines.size()) - 1;
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0
          : min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      afterDel.setPos(pos);
    }

    if (isGoalReached(lines)) {
      mode.onLinewiseGoal(afterDel, base, line, count, baseKS, effort);
      return;
    }

    double hCost = ctx.heuristicCost(lines);
    exploreLinewiseNonGoal(afterDel, base, needsKEscape, count, baseKS, effort, hCost);
  };

  // Join handler
  auto exploreJoin = [&](const EditState& base, bool addSpace,
                         int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterJn = base.afterJoin(addSpace);

    if (isGoalReached(afterJn.getLines())) {
      mode.onJoinGoal(afterJn, base, count, baseKS, effort);
      return;
    }

    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, hCost);
  };

  // Counted linewise handler (dj, dk, {n}dd)
  auto exploreCountedLinewise = [&](const EditState& base, LineRange range,
                                     int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
    bool hasLinesBelow = editBoundary.hasLinesBelow();
    EditState afterDel = base.afterMultiLinewiseDeletion(range, hasLinesBelow);
    const Lines& lines = afterDel.getLines();

    // Detect and resolve past-end cursor before goal check (same as exploreLinewise).
    bool needsKEscape = afterDel.getPos().line >= static_cast<int>(lines.size());
    if (needsKEscape) {
      Position pos = afterDel.getPos();
      pos.line = static_cast<int>(lines.size()) - 1;
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0
          : min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      afterDel.setPos(pos);
    }

    if (isGoalReached(lines)) {
      mode.onCountedLinewiseGoal(afterDel, base, range, count, baseKS, effort);
      return;
    }

    double hCost = ctx.heuristicCost(lines);
    exploreLinewiseNonGoal(afterDel, base, needsKEscape, count, baseKS, effort, hCost);
  };

  // Counted join handler ({n}J, {n}gJ)
  auto exploreCountedJoin = [&](const EditState& base, int count, bool addSpace,
                                 const KeyedSequence& baseKS, const RunningEffort& effort) {
    EditState afterJn = base.afterMultiJoin(count, addSpace);

    if (isGoalReached(afterJn.getLines())) {
      mode.onCountedJoinGoal(afterJn, base, count, baseKS, effort);
      return;
    }

    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, count, baseKS, effort, hCost);
  };

  // ---- Main search loop ----
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    ctx.trackState(s);

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

    if (mode.tryUseSuffixCache(s, results)) continue;

    // Boundary region: cursor is in prefix/suffix — only escape motions
    // and safe backward word edits from first suffix col.
    auto deletionCb = [&](const Range& range, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
      exploreDeletion(s, range, count, baseKS, effort);
    };
    auto motionCb = [&](const Position& newPos, const KeyedSequence& ks, const RunningEffort& effort) {
      EditState newState = s;
      newState.setPos(newPos);
      newState.recordSearch(ks.seq.view(), effort, ctx.effortWeight,
                            ctx.heuristicCost(newState.getLines()), config);
      ctx.exploreNewState(std::move(newState));
    };

    if (ctx.exploreBoundaryEscape(s, deletionCb, motionCb)) continue;

    // Past this point, cursor is guaranteed inside the edit region.
    assert(!ctx.inBoundaryRegion(s.getPos(), s.getLines()));

    // Explore all deletions (characterwise + linewise)
    ctx.exploreAllDeletions(
      s,
      deletionCb,
      [&](int line, int count, const KeyedSequence& baseKS, const RunningEffort& effort) {
        exploreLinewise(s, line, count, baseKS, effort);
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
    ctx.exploreCountedWordEdits(s, deletionCb);
    ctx.exploreCountedCharEdits(s, deletionCb);
  }

  return mode.finalize(std::move(results), initialLines, goalLines, params,
                       bufferFirstLine, bufferFirstCol, goalPos);
}

// Explicit template instantiations
template EditResult EditOptimizer::optimizeImpl<true>(
    const Lines&, const Lines&, EditBoundary, EditOptimizerParams,
    int, int, Position);
template EditResult EditOptimizer::optimizeImpl<false>(
    const Lines&, const Lines&, EditBoundary, EditOptimizerParams,
    int, int, Position);

// =============================================================================
// Public dispatchers
// =============================================================================


EditResult
EditOptimizer::optimizeEdit(
    const Lines &initialLines, const Lines &goalLines,
    EditBoundary editBoundary, EditOptimizerParams params,
    int bufferFirstLine, int bufferFirstCol, Position goalPos) {
  assert(initialLines != goalLines);
  assert(!initialLines.empty());

  // Delegate to pure deletion if goal lines effectively empty
  if (all_of(goalLines.begin(), goalLines.end(), [](Line l) {
    return l.empty();
  })) {
    return optimizeImpl<true>(initialLines, Lines{}, editBoundary, params,
                                bufferFirstLine, bufferFirstCol, goalPos);
  }

  return optimizeImpl<false>(initialLines, goalLines, editBoundary, params,
                             bufferFirstLine, bufferFirstCol, goalPos);
}
