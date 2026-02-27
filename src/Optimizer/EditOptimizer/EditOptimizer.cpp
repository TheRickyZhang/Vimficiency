// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "EditSearchContext.h"
#include "SuffixCache.h"

#include "Interpreter/EditInterpreter.h"
#include "Keyboard/PhysicalKeys.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/ToKeys/MotionToKeys.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Effort/RunningEffort.h"
#include "Optimizer/Indentation.h"


using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

EditResult::EditResult(vector<Result> results, EditSearchStats stats,
                       const Lines& initialLines, int bufferBeginLine,
                       int bufferBeginCol, CursorPos goalPos)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      goalPos_(goalPos),
      beginLine_(bufferBeginLine),
      beginCol_(bufferBeginCol) {
  lineBaseIndex_.reserve(initialLines.size());
  int cumSum = 0;
  for (size_t i = 0; i < initialLines.size(); i++) {
    int colOffset = (i == 0) ? beginCol_ : 0;
    lineBaseIndex_.push_back(cumSum - colOffset);
    // Empty lines have 1 cursor position (col 0), matching initStartingPositions
    int positions = initialLines[i].empty() ? 1 : static_cast<int>(initialLines[i].size());
    cumSum += positions;
  }
}

ostream& operator<<(ostream& os, const EditResult& editResult) {
  os << editResult.stats_ << " goalPos=" << editResult.goalPos_ << "\n";
  for (size_t i = 0; i < editResult.results_.size(); i++) {
    const auto& res = editResult.results_[i];
    if (res.isValid()) {
      os << "  [" << i << "] " << res << "\n";
    }
  }
  return os;
}

namespace {

// Build collapse sequence to merge multi-line goal state into single line.
// <BS> joins current line with previous, <Del> joins with next.
KeyedSequence buildCollapseSequence(int totalLines, int cursorLine) {
  assert(cursorLine >= 0 && cursorLine < totalLines &&
         "cursorLine must be within [0, totalLines)");
  KeyedSequence ks;
  ks.append(KeyedSequence::BS, cursorLine);
  ks.append(KeyedSequence::Del, totalLines - 1 - cursorLine);
  return ks;
}

void appendOptionalCount(KeyedSequence& out, int count, const KeyedSequence& base) {
  assert(count >= 0 && count <= MAX_PREFIX_COUNT);
  if (count <= 1) {
    out += base;
  } else {
    out.appendCounted(count, base);
  }
}

KeyedSequence withOptionalCount(int count, const KeyedSequence& base) {
  assert(count >= 0 && count <= MAX_PREFIX_COUNT);
  if (count <= 1) {
    return base;
  }
  return KeyedSequence(count, base);
}

// Convert a delete command payload to its change-equivalent command.
KeyedSequence deleteToChangeChar(const SequenceBinding& sourceCmd) {
  int count = sourceCmd.count;
  const KeyedSequence& baseKS = sourceCmd.base;
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
    appendOptionalCount(result, count, baseKS);
    result += KeyedSequence::i;
    return result;
  } else if (baseCmd.size() > 1 && baseCmd[0] == 'd') {
    result = baseKS.asChange();
  } else {
    assert(false && "unsupported command in EditOptimizer!");
    return {};
  }
  return withOptionalCount(count, result);
}

// Convert a linewise delete command payload to its change-equivalent command.
KeyedSequence deleteToChangeLine(const SequenceBinding& sourceCmd,
                                 string_view lineContent) {
  int count = sourceCmd.count;
  const KeyedSequence& baseKS = sourceCmd.base;
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
    return withOptionalCount(count, KeyedSequence::cc);
  }

  // dj → cj, dk → ck
  return withOptionalCount(count, baseKS.asChange());
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
    if (dist <= 2) ks.append(KeyedSequence::l, dist);
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
    ks.append(inserted[diff[i]]);

    // Navigate to next run
    i = j + 1;
    if (i < diff.size()) {
      int dist = diff[i] - diff[j];
      if (dist <= 2) {
        ks.append(KeyedSequence::l, dist);
      } else {
        char findChar = deleted[diff[i]];
        int occurrences = count(deleted.begin() + diff[j] + 1,
                                deleted.begin() + diff[i], findChar);
        if (occurrences == 0) {
          ks += KeyedSequence::f;
          ks.append(findChar);
        } else {
          ks.appendCounted(dist, KeyedSequence::l);
        }
      }
    }
  }

  // CursorPos cursor at end of inserted text
  int lastDiff = diff.back();
  int endPos = static_cast<int>(inserted.size()) - 1;
  if (lastDiff < endPos) appendNav(endPos - lastDiff);

  RunningEffort effort(ks.keys, config);
  double totalEffort = effort.getEffort(config);
  if (totalEffort > maxEffort) return nullopt;

  return Result(std::move(ks.seq), totalEffort);
}

RunningEffort mergeGoalSuffixEffort(const KeyedSequence& prefix,
                                    const RunningEffort& typedSuffixEffort,
                                    double completionPenalty,
                                    const Config& config) {
  RunningEffort prefixEffort(prefix.keys, config);
  if (completionPenalty > 0.0) {
    prefixEffort.addPenalty(completionPenalty);
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
                      const SequenceBinding& sourceCmd) {
    ctx.exploreWithDot(std::move(afterDel), base, sourceCmd, 0.0);
  }

  void onLinewiseGoal(EditState& afterDel, const EditState& base, int,
                      const SequenceBinding& sourceCmd) {
    ctx.exploreWithDot(std::move(afterDel), base, sourceCmd, 0.0);
  }

  void onJoinGoal(EditState& afterJn, const EditState& base,
                  const SequenceBinding& sourceCmd) {
    ctx.exploreWithDot(std::move(afterJn), base, sourceCmd, 0.0);
  }

  void onCountedLinewiseGoal(EditState& afterDel, const EditState& base,
                             LineRange, const SequenceBinding& sourceCmd) {
    ctx.exploreWithDot(std::move(afterDel), base, sourceCmd, 0.0);
  }

  void onCountedJoinGoal(EditState& afterJn, const EditState& base,
                         const SequenceBinding& sourceCmd) {
    ctx.exploreWithDot(std::move(afterJn), base, sourceCmd, 0.0);
  }

  EditResult finalize(vector<Result>&& results, const Lines& initialLines,
                      const Lines&, const EditOptimizerParams& params,
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos) {
    // Try visual mode deletion: v{motion}d from first content position to last
    if (ctx.effectiveLines.size() > 1 ||
        static_cast<int>(ctx.effectiveLines[0].size()) > ctx.leftColOffset + ctx.rightColOffset) {
      CursorPos beginPos(0, ctx.leftColOffset);

      int lastLine = ctx.effectiveLines.lastLine();
      int lastCol = static_cast<int>(ctx.effectiveLines[lastLine].size()) - 1 - ctx.rightColOffset;
      CursorPos lastPos(lastLine, max(0, lastCol));

      if (lastPos > beginPos || (lastPos.line == beginPos.line && lastPos.col > beginPos.col)) {
        MotionOptimizer motionOpt(config);

        auto motionResult = motionOpt.optimize(
            ctx.effectiveLines,
            beginPos,
            lastPos,
            MotionOptimizerParams{}
                .withLinePaddingAbove(params.motionLinePaddingAbove)
                .withLinePaddingBelow(params.motionLinePaddingBelow)
                .withMinCountRepeat(params.minPrefixCount)
                .withMaxCountRepeat(params.maxPrefixCount)
        );

        const auto& motionResults = motionResult.getResults();
        if (!motionResults.empty() && motionResults[0].isValid()) {
          Sequence visualSeq("v");
          visualSeq.append(motionResults[0].getSequence().view());
          visualSeq.append("d");

          static const PhysicalKeys vKey = {Key::Key_V};
          static const PhysicalKeys dKey = {Key::Key_D};
          RunningEffort effort(vKey, config);
          effort.append(globalSequenceToKeys().tokenize(motionResults[0].getSequence().view()), config);
          double totalEffort = effort.append(dKey, config);

          if (!results[0].isValid() || totalEffort < results[0].getCost()) {
            results[0] = Result(std::move(visualSeq), totalEffort);
          }
        }
      }
    }

    return EditResult(std::move(results), ctx.getStats(), initialLines,
                      bufferBeginLine, bufferBeginCol, goalPos);
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
    typedEffort = RunningEffort(typed.keys, config);

    if constexpr (VimOptions::autoindent()) {
      goalFirstIndentLen = leadingSpaceCount(goalLines[0]);
      if (goalFirstIndentLen > 0) {
        // typed starts with goalFirstIndentLen literal spaces — slice to get
        // the version that assumes autoindent provides them.
        typedAfterIndent = KeyedSequence(
            typed.seq.view().substr(goalFirstIndentLen),
            PhysicalKeys(typed.keys.view().subspan(goalFirstIndentLen)));
        afterIndentEffort = RunningEffort(typedAfterIndent.keys, config);
      }
    }
  }

  // Comprised of empty lines
  bool isGoalReached(const Lines& lines) const {
    if (lines.size() == 1) return lines[0] == preSuf;
    if (lines[0] != pre) return false;
    if (lines.back() != suf) return false;
    for (size_t i = 1; i < lines.size() - 1; i++) {
      if (!lines[i].empty()) return false;
    }
    return true;
  }

  bool isDotRepeat(const EditState& base, const SequenceBinding& sourceCmd) const {
    return base.hasLastEdit() &&
           base.getLastEditCount() == sourceCmd.count &&
           base.getLastEditBase() == sourceCmd.base.seq.view();
  }

  vector<KeyedSequence> buildKeyedSequencesFromParsedEdits(const vector<ParsedEdit>& edits) const {
    int n = static_cast<int>(edits.size());
    vector<KeyedSequence> res(n);
    for (int i = 0; i < n; i++) {
      string editStr;
      if (edits[i].hasCount()) {
        editStr = to_string(edits[i].effectiveCount()) + string(edits[i].edit);
      } else {
        editStr = string(edits[i].edit);
      }
      res[i] = KeyedSequence(editStr, globalSequenceToKeys().tokenize(editStr));
    }
    return res;
  }

  vector<RunningEffort> buildRawSuffixEfforts(const SuffixProgram& program,
                                              const RunningEffort& terminalSuffixEffort,
                                              int extraPenaltyIndex,
                                              double extraPenalty) const {
    int n = program.size();
    vector<RunningEffort> suffixEfforts(n + 1);
    suffixEfforts[n] = terminalSuffixEffort;

    // Context-free tail table: suffixEfforts[i] is the effort of edits[i..] + terminal suffix
    // with '.' kept literal. Per-state dot expansion composes on top of this.
    for (int i = n - 1; i >= 0; i--) {
      RunningEffort editEffort(program.editCmds[i].keys, config);
      if (i == extraPenaltyIndex && extraPenalty > 0.0) {
        editEffort.addPenalty(extraPenalty);
      }
      suffixEfforts[i] = RunningEffort::merge(editEffort, suffixEfforts[i + 1]);
    }
    return suffixEfforts;
  }

  SuffixValue buildSuffixValueForNextIndex(
      const shared_ptr<const SuffixProgram>& suffixProgram,
      const vector<ParsedEdit>& dotAwareEdits,
      const vector<RunningEffort>& rawSuffixEfforts,
      int nextIndex,
      const string& lastEditCmd) const {
    int programSize = suffixProgram->size();
    assert(nextIndex >= 0 && nextIndex <= programSize);

    // Default path is always context-independent. Dot expansion only applies
    // when this index maps to a parsed edit token in dotAwareEdits.
    if (nextIndex >= static_cast<int>(dotAwareEdits.size()) ||
        dotAwareEdits[nextIndex].edit != "." || lastEditCmd.empty()) {
      return SuffixValue(suffixProgram, nextIndex, rawSuffixEfforts[nextIndex]);
    }

    // Leading dot at nextIndex:
    // - Expanded default variant: {lastEditCmd} + edits[nextIndex+1..]
    // - Optional dot override: edits[nextIndex..] (for matching last-edit context)
    // lastEditCmd must come from replay semantics; it is not simply previous parsed token.
    // Example: "dwj." -> previous token before '.' is "j", but dot repeats "dw".
    KeyedSequence expandedPrefix(lastEditCmd, globalSequenceToKeys().tokenize(lastEditCmd));
    RunningEffort expandedPrefixEffort(expandedPrefix.keys, config);

    // Possible optimization (high confidence): cache RunningEffort for repeated
    // lastEditCmd strings within this replay call to avoid recomputing prefix effort.
    RunningEffort expandedEffort =
        RunningEffort::merge(expandedPrefixEffort, rawSuffixEfforts[nextIndex + 1]);

    return SuffixValue(
        suffixProgram, nextIndex + 1, std::move(expandedPrefix), expandedEffort,
        lastEditCmd, nextIndex, rawSuffixEfforts[nextIndex]);
  }

  // TODO: verify semantics and performance
  //
  // Build suffix-cache entries for every intermediate state reached while replaying
  // the discovered search sequence from one seed position.
  //
  // Preconditions:
  // - searchPrefixSeq is the already-discovered prefix from seed -> base state.
  // - completionSuffix is the synthesized final edit segment from base state.
  // - typedSuffix is the terminal insert payload after completionSuffix.
  //   Example: final "ciwapple<Esc>" can be split as:
  //     searchPrefixSeq="", completionSuffix="ciw", typedSuffix="apple<Esc>".
  //
  // Cache invariant: from a cached key (lines hash + size + pos + mode), the stored
  // suffix is a valid continuation to goal, with matching pre-computed effort.
  void replayAndCacheSuffix(int startIndex, const string& searchPrefixSeq,
                            const KeyedSequence& completionSuffix,
                            double completionPenalty,
                            const KeyedSequence& typedSuffix,
                            const RunningEffort& typedSuffixEffort) {
    cachePopulations++;

    // Flow:
    // - Build semantic edit program: [searchPrefix edits] + [completionSuffix as one edit].
    // - Use typedSuffix as the terminal suffix.
    // - Replay only the searchPrefix edits to recover exact state keys/lastEditCmd.
    vector<ParsedEdit> prefixEdits = Edit::parseEdits(searchPrefixSeq);
    int prefixEditCount = static_cast<int>(prefixEdits.size());
    vector<KeyedSequence> replayEditCmds = buildKeyedSequencesFromParsedEdits(prefixEdits);
    replayEditCmds.push_back(completionSuffix);
    int programEditCount = static_cast<int>(replayEditCmds.size());
    // Shared across many cache entries from this replay.
    auto suffixProgram = make_shared<SuffixProgram>(std::move(replayEditCmds), typedSuffix);

    // Raw tail efforts intentionally keep '.' unexpanded; expansion needs per-state lastEditCmd.
    // completionPenalty is attached at the completion edit (index prefixEditCount).
    vector<RunningEffort> rawSuffixEfforts =
        buildRawSuffixEfforts(*suffixProgram, typedSuffixEffort,
                              prefixEditCount, completionPenalty);

    // 3) Initialize replay state at this seed position.
    Lines replayLines = ctx.effectiveLines;
    CursorPos replayPos = ctx.seedPositionFor(startIndex, initialLines);
    // applyEdit mutates mode by reference; replay starts in Normal.
    Mode replayMode = Mode::Normal;

    // 4) Cache current state, then replay one prefix edit.
    // We cache programEditCount indices (0..programEditCount-1):
    // - 0..prefixEditCount-1: remaining searchPrefix edits + completion + typed
    // - prefixEditCount: completion + typed
    // typed-only index is intentionally skipped.
    //
    // lastEditCmd is maintained by applyEdit using real dot-repeat semantics.
    // This is not trivially prefixEdits[nextIndex-1] (e.g. in "dwj.", lastEditCmd at '.'
    // is "dw", not "j").
    string lastEditCmd;
    for (int nextIndex = 0; nextIndex < programEditCount; nextIndex++) {
      size_t replayHash = hashLines(replayLines);
      SuffixKey sk(replayHash, static_cast<int>(replayLines.size()), replayPos, replayMode);
      if (suffixCache.find(sk) == suffixCache.end()) {
        suffixCache[sk] = buildSuffixValueForNextIndex(
            suffixProgram, prefixEdits, rawSuffixEfforts, nextIndex, lastEditCmd);
      }

      if (nextIndex >= prefixEditCount) break;

      Edit::applyEdit(replayLines, replayPos, replayMode, prefixEdits[nextIndex], &lastEditCmd,
                      editBoundary.hasLinesBelow(),
                      ctx.leftColOffset, ctx.rightColOffset,
                      editBoundary.hasLinesAbove());
      // if (replayMode == Mode::Insert) break;
    }
  }

  KeyedSequence buildChangePrefix(const SequenceBinding& sourceCmd,
                                  const Lines& postDelLines, const CursorPos& postDelPos,
                                  const Lines& preDelLines, const Range& range) const {
    int totalLines = static_cast<int>(postDelLines.size());
    int cursorLine = postDelPos.line;

    bool rangeEndsAtLineEnd = false;
    if (range.isValid() && !range.isEmpty()) {
      if (range.end.col > 0) {
        rangeEndsAtLineEnd =
            range.end.col >= static_cast<int>(preDelLines[range.end.line].size());
      } else if (range.end.line > 0) {
        // Half-open end at col 0 means range already consumed the previous line
        // through its end.
        rangeEndsAtLineEnd = true;
      }
    }

    if (range.spansMultiple() &&
        range.begin.col == 0 &&
        rangeEndsAtLineEnd) {
      if (preDelLines.size() - range.size() > 0) {
        totalLines++;
        cursorLine++;
      }
    }

    KeyedSequence prefix = deleteToChangeChar(sourceCmd);
    prefix += buildCollapseSequence(totalLines, cursorLine);
    return prefix;
  }

  struct KeyedSegment {
    const KeyedSequence& sequence;
    const RunningEffort& effort;
  };

  struct TypedGoalVariants {
    KeyedSegment normalTyped;
    KeyedSegment dotTyped;
  };

  struct GoalSuffixPath {
    KeyedSequence sequence;
    RunningEffort effort;
  };

  GoalSuffixPath buildGoalSuffixPath(const KeyedSequence& goalCompletionCmd,
                                     const KeyedSegment& typedCmd,
                                     double completionPenalty) const {
    GoalSuffixPath path;
    path.effort = mergeGoalSuffixEffort(
        goalCompletionCmd, typedCmd.effort, completionPenalty, config);
    path.sequence = goalCompletionCmd;
    path.sequence += typedCmd.sequence;
    return path;
  }

  GoalSuffixPath buildDotGoalSuffixPath(const EditState& postCompletionState,
                                        const KeyedSegment& dotTyped) const {
    GoalSuffixPath path;
    path.sequence = KeyedSequence(".", KeyedSequence::Period.keys);
    path.sequence += KeyedSequence::i;
    path.sequence += buildCollapseSequence(
        static_cast<int>(postCompletionState.getLines().size()),
        postCompletionState.getPos().line);

    RunningEffort dotPrefixEffort(path.sequence.keys, config);
    path.effort = RunningEffort::merge(dotPrefixEffort, dotTyped.effort);

    path.sequence += dotTyped.sequence;
    return path;
  }

  // Emit goal via d→c conversion + suffix cache + optional dot goal path.
  void emitEditGoal(EditState& postCompletionState, const EditState& base,
                    const SequenceBinding& sourceCmd,
                    const KeyedSequence& goalCompletionCmd,
                    const TypedGoalVariants& typedVariants,
                    bool allowDotGoalPath) {
    GoalSuffixPath normalGoal = buildGoalSuffixPath(
        goalCompletionCmd, typedVariants.normalTyped, sourceCmd.effort.getPenalty());

    // Normal goal path also seeds suffix cache.
    replayAndCacheSuffix(base.getStartIndex(), base.getSeq(),
                         goalCompletionCmd, sourceCmd.effort.getPenalty(),
                         typedVariants.normalTyped.sequence, typedVariants.normalTyped.effort);

    EditState normalState = postCompletionState;
    normalState.recordSearch(normalGoal.sequence.seq.view(), normalGoal.effort,
                             ctx.effortWeight, 0.0, config);
    normalState.setLastEdit(sourceCmd.count, sourceCmd.base.seq.view());
    ctx.exploreNewState(std::move(normalState));

    // Dot goal path: . + i + collapse + typed (skip replayAndCacheSuffix).
    // The dot repeats the deletion, then 'i' enters insert mode without autoindent.
    if (allowDotGoalPath) {
      GoalSuffixPath dotGoal = buildDotGoalSuffixPath(postCompletionState, typedVariants.dotTyped);

      EditState dotState = std::move(postCompletionState);
      dotState.recordSearch(dotGoal.sequence.seq.view(), dotGoal.effort,
                            ctx.effortWeight, 0.0, config);
      ctx.exploreNewState(std::move(dotState));
    }
  }

  void onDeletionGoal(EditState& afterDel, const EditState& base, const Range& range,
                      const SequenceBinding& sourceCmd) {
    bool isDot = isDotRepeat(base, sourceCmd);
    KeyedSequence goalCompletionCmd = buildChangePrefix(sourceCmd,
                                                        afterDel.getLines(), afterDel.getPos(),
                                                        base.getLines(), range);
    TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
    emitEditGoal(afterDel, base, sourceCmd, goalCompletionCmd, typedVariants, isDot);
  }

  void onLinewiseGoal(EditState& afterDel, const EditState& base, int line,
                      const SequenceBinding& sourceCmd) {
    bool isDot = isDotRepeat(base, sourceCmd);
    int ccLineCount = static_cast<int>(base.getLines().size());
    KeyedSequence changeCmd = deleteToChangeLine(sourceCmd, base.getLines()[line]);
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
      changePrefix.append(KeyedSequence::BS, bsClear + line);
      changePrefix.append(KeyedSequence::Del, ccLineCount - 1 - line);
    } else {
      changePrefix += buildCollapseSequence(ccLineCount, line);
    }

    const auto& suffixTyped = useAfterIndent ? typedAfterIndent : typed;
    const auto& suffixEffort = useAfterIndent ? afterIndentEffort : typedEffort;
    // Dot path always uses full typed content because ".i" does not autoindent.
    TypedGoalVariants typedVariants{{suffixTyped, suffixEffort}, {typed, typedEffort}};
    emitEditGoal(afterDel, base, sourceCmd, changePrefix, typedVariants, isDot);
  }

  void onJoinGoal(EditState& afterJn, const EditState& base,
                  const SequenceBinding& sourceCmd) {
    bool isDot = isDotRepeat(base, sourceCmd);
    const auto& iCmd = KeyedSequence::i;
    KeyedSequence goalCompletionCmd;
    appendOptionalCount(goalCompletionCmd, sourceCmd.count, sourceCmd.base);
    goalCompletionCmd += iCmd;
    goalCompletionCmd += buildCollapseSequence(
        static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
    TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
    emitEditGoal(afterJn, base, sourceCmd, goalCompletionCmd, typedVariants, isDot);
  }

  void onCountedLinewiseGoal(EditState& afterDel, const EditState& base,
                             LineRange range, const SequenceBinding& sourceCmd) {
    bool isDot = isDotRepeat(base, sourceCmd);
    int lineCount = range.endLine - range.beginLine;
    int ccLineCount = static_cast<int>(base.getLines().size()) - lineCount + 1;
    KeyedSequence changeCmd = deleteToChangeLine(sourceCmd, base.getLines()[range.beginLine]);
    // Counted linewise changes ({n}cc, cj, ck) are always linewise — autoindent applies
    int autoindentLen = 0;
    if constexpr (VimOptions::autoindent()) {
      autoindentLen = leadingSpaceCount(base.getLines()[range.beginLine]);
    }
    bool needsCollapse = ccLineCount > 1;

    KeyedSequence changePrefix = changeCmd;
    bool useAfterIndent = false;
    // Collapse BS×cursorLine + Del×rest. Only BS interacts with autoindent.
    bool bsInCollapse = needsCollapse && range.beginLine > 0;
    if constexpr (VimOptions::autoindent()) {
      if (!bsInCollapse) {
        // Safe: no BS in collapse. Adjust autoindent to goal indent.
        changePrefix += computeIndentAdjustment(autoindentLen, goalFirstIndentLen);
        changePrefix += buildCollapseSequence(ccLineCount, range.beginLine);
        useAfterIndent = goalFirstIndentLen > 0;
      } else {
        // BS in collapse: account for autoindent in BS count.
        int bsClear = autoindentLen > 0 ? bsCountForIndent(autoindentLen, 0) : 0;
        changePrefix.append(KeyedSequence::BS, bsClear + range.beginLine);
        changePrefix.append(KeyedSequence::Del, ccLineCount - 1 - range.beginLine);
      }
    } else {
      changePrefix += buildCollapseSequence(ccLineCount, range.beginLine);
    }

    const auto& suffixTyped = useAfterIndent ? typedAfterIndent : typed;
    const auto& suffixEffort = useAfterIndent ? afterIndentEffort : typedEffort;
    // Dot path always uses full typed content because ".i" does not autoindent.
    TypedGoalVariants typedVariants{{suffixTyped, suffixEffort}, {typed, typedEffort}};
    emitEditGoal(afterDel, base, sourceCmd, changePrefix, typedVariants, isDot);
  }

  void onCountedJoinGoal(EditState& afterJn, const EditState& base,
                         const SequenceBinding& sourceCmd) {
    bool isDot = isDotRepeat(base, sourceCmd);
    const auto& iCmd = KeyedSequence::i;
    KeyedSequence goalCompletionCmd;
    appendOptionalCount(goalCompletionCmd, sourceCmd.count, sourceCmd.base);
    goalCompletionCmd += iCmd;
    goalCompletionCmd += buildCollapseSequence(
        static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
    TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
    emitEditGoal(afterJn, base, sourceCmd, goalCompletionCmd, typedVariants, isDot);
  }

  bool tryUseSuffixCache(const EditState& s, vector<Result>& results) {
    SuffixKey sk(s.getLinesHash(), static_cast<int>(s.getLines().size()), s.getPos(), s.getMode());
    auto cacheIt = suffixCache.find(sk);
    if (cacheIt == suffixCache.end()) return false;

    cacheHits++;
    int idx = s.getStartIndex();
    if (!results[idx].isValid()) {
      const SuffixValue& sv = cacheIt->second;

      bool useDot = sv.canUseDot(s.getLastEditCount(), s.getLastEditBase());
      const KeyedSequence& suffix = sv.suffix(useDot);
      const RunningEffort& suffixEffort = sv.suffixEffort(useDot);

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
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos) {
    // Try replacement strategy (same-length, single-line) with A* budget
    if (results[0].isValid() &&
        initialLines.size() == 1 && goalLines.size() == 1 &&
        initialLines[0].size() == goalLines[0].size()) {
      auto replacementResult = tryReplacement(initialLines[0], goalLines[0],
                                              config, results[0].getCost());
      if (replacementResult.has_value()) {
        results[0] = *replacementResult;
      }
    }

    EditSearchStats stats = ctx.getStats();
    stats.cacheHits = cacheHits;
    stats.cacheEntries = static_cast<int>(suffixCache.size());
    stats.cachePopulations = cachePopulations;

    return EditResult(std::move(results), stats, initialLines,
                      bufferBeginLine, bufferBeginCol, goalPos);
  }
};

} // anonymous namespace

template<bool Capture>
struct PureDeletionGoalCapture;

template<>
struct PureDeletionGoalCapture<false> {
  explicit PureDeletionGoalCapture(int) {}

  void onGoal(int, const CursorPos&) {}

  EditResult finalize(EditResult&& editResult, int) {
    return std::move(editResult);
  }
};

template<>
struct PureDeletionGoalCapture<true> {
  vector<CursorPos> goalPosByStart;

  explicit PureDeletionGoalCapture(int totalPositions)
      : goalPosByStart(static_cast<size_t>(totalPositions), CursorPos(-1, -1, -1)) {}

  void onGoal(int idx, const CursorPos& pos) {
    goalPosByStart[static_cast<size_t>(idx)] = pos;
  }

  EditResult finalize(EditResult&& editResult, int bufferBeginLine) {
    // Convert per-result positions from edit-local coordinates to buffer
    // coordinates. For line-OOB pure deletion terminals, line maps to the
    // corresponding line below the edit region in full-buffer coordinates;
    // column is clamped later where full post-edit lines are available.
    for (CursorPos& p : goalPosByStart) {
      if (p.line < 0) continue;
      p.line += bufferBeginLine;
    }
    editResult.setGoalPosByStart(std::move(goalPosByStart));
    return std::move(editResult);
  }
};

// =============================================================================
// =============================================================================
// optimizeImpl - unified template for both pure deletion and full edit
// =============================================================================

template<bool PureDeletion>
EditResult EditOptimizer::optimizeImpl(const Lines &initialLines, const Lines &goalLines,
                                       EditBoundary editBoundary, EditOptimizerParams params,
                                       int bufferBeginLine, int bufferBeginCol,
                                       CursorPos goalPos) {
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(initialLines, editBoundary, params, config);
  ctx.initStartingPositions(initialLines);

  // Local aliases for goal checking
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  const string preSuf = pre + suf;

  vector<Result> results(ctx.totalPositions);
  PureDeletionGoalCapture<PureDeletion> goalCapture(ctx.totalPositions);
  ModePolicy<PureDeletion> mode(ctx, config, editBoundary, initialLines, goalLines,
                                pre, suf, preSuf);

  // Goal check
  auto isGoalReached = [&](const Lines &lines) -> bool {
    return mode.isGoalReached(lines);
  };

  // ---- Handlers ----

  // Deletion handler
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const SequenceBinding& sourceCmd) {
    EditState afterDel = base.afterDeletion(range);
    const Lines &lines = afterDel.getLines();

    if (isGoalReached(lines)) {
      mode.onDeletionGoal(afterDel, base, range, sourceCmd);
      return;
    }

    double hCost = ctx.heuristicCost(lines);
    ctx.exploreWithDot(std::move(afterDel), base, sourceCmd, hCost);
  };

  // Linewise handler
  auto exploreLinewise = [&](const EditState &base, int line,
                             const SequenceBinding& sourceCmd) {
    bool hasLinesBelow = editBoundary.hasLinesBelow();
    EditState afterDel = base.afterLinewiseDeletion(line, hasLinesBelow);
    const Lines &lines = afterDel.getLines();

    // Linewise delete can land one line below effective lines when hasLinesBelow=true.
    // Policy:
    // - Pure deletion may accept this only as a terminal goal transition.
    // - All non-goal transitions must keep cursor line in effective bounds.
    bool lineOOB = afterDel.getPos().line < 0 ||
                   afterDel.getPos().line >= static_cast<int>(lines.size());

    if (isGoalReached(lines)) {
      if (lineOOB) {
        if constexpr (!PureDeletion) {
          return;
        }
      }
      mode.onLinewiseGoal(afterDel, base, line, sourceCmd);
      return;
    }

    if (lineOOB) return;

    double hCost = ctx.heuristicCost(lines);
    ctx.exploreWithDot(std::move(afterDel), base, sourceCmd, hCost);
  };

  // Join handler
  auto exploreJoin = [&](const EditState& base, bool addSpace,
                         const SequenceBinding& sourceCmd) {
    EditState afterJn = base.afterJoin(addSpace);

    if (isGoalReached(afterJn.getLines())) {
      mode.onJoinGoal(afterJn, base, sourceCmd);
      return;
    }

    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, sourceCmd, hCost);
  };

  // Counted linewise handler (dj, dk, {n}dd)
  auto exploreCountedLinewise = [&](const EditState& base, LineRange range,
                                     const SequenceBinding& sourceCmd) {
    bool hasLinesBelow = editBoundary.hasLinesBelow();
    EditState afterDel = base.afterMultiLinewiseDeletion(range, hasLinesBelow);
    const Lines& lines = afterDel.getLines();

    // Same policy as uncounted linewise delete.
    bool lineOOB = afterDel.getPos().line < 0 ||
                   afterDel.getPos().line >= static_cast<int>(lines.size());

    if (isGoalReached(lines)) {
      if (lineOOB) {
        if constexpr (!PureDeletion) {
          return;
        }
      }
      mode.onCountedLinewiseGoal(afterDel, base, range, sourceCmd);
      return;
    }

    if (lineOOB) return;

    double hCost = ctx.heuristicCost(lines);
    ctx.exploreWithDot(std::move(afterDel), base, sourceCmd, hCost);
  };

  // Counted join handler ({n}J, {n}gJ)
  auto exploreCountedJoin = [&](const EditState& base, bool addSpace,
                                const SequenceBinding& sourceCmd) {
    EditState afterJn = base.afterMultiJoin(sourceCmd.count, addSpace);

    if (isGoalReached(afterJn.getLines())) {
      mode.onCountedJoinGoal(afterJn, base, sourceCmd);
      return;
    }

    double hCost = ctx.heuristicCost(afterJn.getLines());
    ctx.exploreWithDot(std::move(afterJn), base, sourceCmd, hCost);
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
        goalCapture.onGoal(idx, s.getPos());
        ctx.resultsFound++;
        ctx.uniquePositionsCovered++;
      }
      continue;
    }

    assert(s.getPos().line >= 0 &&
           s.getPos().line <= s.getLines().lastLine() &&
           "non-goal edit state must have in-bounds cursor line");

    // Early stopping: skip if this startIndex already has a result
    if (results[s.getStartIndex()].isValid()) continue;

    if (mode.tryUseSuffixCache(s, results)) continue;

    // Boundary region: cursor is in prefix/suffix — only escape motions
    // and safe backward word edits from first suffix col.
    auto deletionCb = [&](const Range& range, const SequenceBinding& sourceCmd) {
      exploreDeletion(s, range, sourceCmd);
    };
    auto motionCb = [&](const CursorPos& newPos, const SequenceBinding& motionCmd) {
      assert(motionCmd.count == 0 && "Boundary motions should not carry counts");
      EditState newState = s;
      newState.setPos(newPos);
      newState.recordSearch(motionCmd.base.seq.view(), motionCmd.effort, ctx.effortWeight,
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
      [&](int line, const SequenceBinding& sourceCmd) {
        exploreLinewise(s, line, sourceCmd);
      },
      [&](bool addSpace, const SequenceBinding& sourceCmd) {
        exploreJoin(s, addSpace, sourceCmd);
      }
    );

    // Explore counted operations
    ctx.exploreCountedLineEdits(s,
      [&](LineRange range, const SequenceBinding& sourceCmd) {
        exploreCountedLinewise(s, range, sourceCmd);
      });
    ctx.exploreCountedJoinCommands(s,
      [&](bool addSpace, const SequenceBinding& sourceCmd) {
        exploreCountedJoin(s, addSpace, sourceCmd);
      });
    ctx.exploreCountedWordEdits(s, deletionCb);
    ctx.exploreCountedCharEdits(s, deletionCb);
  }

  EditResult out = mode.finalize(std::move(results), initialLines, goalLines, params,
                                 bufferBeginLine, bufferBeginCol, goalPos);
  return goalCapture.finalize(std::move(out), bufferBeginLine);
}

// Explicit template instantiations
template EditResult EditOptimizer::optimizeImpl<false>(
    const Lines&, const Lines&, EditBoundary, EditOptimizerParams, int, int, CursorPos);
template EditResult EditOptimizer::optimizeImpl<true>(
    const Lines&, const Lines&, EditBoundary, EditOptimizerParams, int, int, CursorPos);

// =============================================================================
// Public dispatchers
// =============================================================================


EditResult
EditOptimizer::optimizeEdit(
    const Lines &initialLines, const Lines &goalLines,
    EditBoundary editBoundary, EditOptimizerParams params,
    int bufferBeginLine, int bufferBeginCol, CursorPos goalPos) {
  assert(initialLines != goalLines);
  assert(!initialLines.empty());
  bool pureDeletionGoal = goalLines.empty() ||
                          (goalLines.size() == 1 && goalLines[0].empty());
  assert(!pureDeletionGoal &&
         "optimizeEdit does not accept empty goalLines; use optimizePureDeletion for pure deletions");
  params.normalizeCountRepeatBounds();

  return optimizeImpl<false>(initialLines, goalLines, editBoundary, params,
                             bufferBeginLine, bufferBeginCol, goalPos);
}

EditResult EditOptimizer::optimizePureDeletion(
    const Lines& initialLines,
    EditBoundary editBoundary,
    EditOptimizerParams params,
    int bufferBeginLine,
    int bufferBeginCol,
    CursorPos goalPos) {
  assert(!initialLines.empty());
  params.normalizeCountRepeatBounds();
  return optimizeImpl<true>(
      initialLines, Lines{}, editBoundary, params,
      bufferBeginLine, bufferBeginCol, goalPos);
}
