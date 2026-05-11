#include "ChangeGoalHandler.h"

#include <algorithm>
#include <cassert>

#include "TransformOptimizer.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/BuildTypedCommands.h"

using namespace std;

ChangeGoalHandler::ChangeGoalHandler(
    const Lines& effectiveLines, int leftColOffset, int rightColOffset,
    double effortWeight, const Config& config,
    const TransformBoundary& transformBoundary, const Lines& initialLines,
    const Lines& goalLines, const string& pre,
    const string& suf, const string& preSuf)
    : effectiveLines(effectiveLines),
      leftColOffset(leftColOffset),
      rightColOffset(rightColOffset),
      effortWeight(effortWeight),
      config(config),
      transformBoundary(transformBoundary),
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
      typedAfterIndent = buildTypedCommands(
          goalLines, leadingWhitespace(goalLines[0]), pre, suf);
      afterIndentEffort = RunningEffort(typedAfterIndent.keys, config);
    }
  }
}

bool ChangeGoalHandler::isGoalReached(const Lines& lines) const {
  if (lines.size() == 1) return lines[0] == preSuf;
  if (lines[0] != pre) return false;
  if (lines.back() != suf) return false;
  for (size_t i = 1; i < lines.size() - 1; i++) {
    if (!lines[i].empty()) return false;
  }
  return true;
}

bool ChangeGoalHandler::isDotRepeat(const TransformState& base, const SequenceBinding& sourceCmd) const {
  return base.hasLastEdit() &&
         base.getLastEditCount() == sourceCmd.count &&
         base.getLastEditBase() == sourceCmd.base.seq.view();
}

// Static helpers

KeyedSequence ChangeGoalHandler::buildCollapseSequence(int totalLines, int cursorLine) {
  assert(cursorLine >= 0 && cursorLine < totalLines &&
         "cursorLine must be within [0, totalLines)");
  KeyedSequence ks;
  ks.append(KeyedSequence::BS, cursorLine);
  ks.append(KeyedSequence::Del, totalLines - 1 - cursorLine);
  return ks;
}

void ChangeGoalHandler::appendOptionalCount(KeyedSequence& out, int count, const KeyedSequence& base) {
  assert(count >= 0 && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
  if (count <= 1) {
    out += base;
  } else {
    out.appendCounted(count, base);
  }
}

KeyedSequence ChangeGoalHandler::withOptionalCount(int count, const KeyedSequence& base) {
  assert(count >= 0 && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
  if (count <= 1) {
    return base;
  }
  return KeyedSequence(count, base);
}

KeyedSequence ChangeGoalHandler::deleteToChangeChar(const SequenceBinding& sourceCmd) {
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
    appendOptionalCount(result, count, baseKS);
    result += KeyedSequence::i;
    return result;
  } else if (baseCmd.size() > 1 && baseCmd[0] == 'd') {
    result = baseKS.asChange();
  } else {
    assert(false && "unsupported command in TransformOptimizer!");
    return {};
  }
  return withOptionalCount(count, result);
}

KeyedSequence ChangeGoalHandler::deleteToChangeLine(const SequenceBinding& sourceCmd,
                                                    string_view lineContent) {
  int count = sourceCmd.count;
  const KeyedSequence& baseKS = sourceCmd.base;
  string_view baseCmd = baseKS.seq.view();

  if (baseCmd == "dd") {
    if (count == 0) {
      if constexpr (VimOptions::autoindent()) {
        if (!leadingWhitespace(lineContent).empty()) {
          return KeyedSequence::ZeroC;
        }
      }
      return KeyedSequence::cc;
    }
    return withOptionalCount(count, KeyedSequence::cc);
  }

  return withOptionalCount(count, baseKS.asChange());
}

RunningEffort ChangeGoalHandler::mergeGoalSuffixEffort(
    const KeyedSequence& prefix,
    const RunningEffort& typedSuffixEffort,
    double completionPenalty,
    const Config& config) {
  RunningEffort prefixEffort(prefix.keys, config);
  if (completionPenalty > 0.0) {
    prefixEffort.addPenalty(completionPenalty);
  }
  return RunningEffort::merge(prefixEffort, typedSuffixEffort);
}

CursorPos ChangeGoalHandler::seedPositionForStart(int startIndex, const Lines& initialLines,
                                                  int leftColOffset) {
  int remaining = startIndex;
  for (int line = 0; line < static_cast<int>(initialLines.size()); line++) {
    int lineSize = initialLines[line].empty() ? 1 : static_cast<int>(initialLines[line].size());
    if (remaining < lineSize) {
      return CursorPos(line, remaining + (line == 0 ? leftColOffset : 0));
    }
    remaining -= lineSize;
  }
  return CursorPos(-1, -1);
}

// Build change prefix overloads

KeyedSequence ChangeGoalHandler::buildChangePrefix(
    const SequenceBinding& sourceCmd,
    const Lines& postDelLines, const CursorPos& postDelPos,
    const Lines& preDelLines, const CharRange& range) const {
  int totalLines = static_cast<int>(postDelLines.size());
  int cursorLine = postDelPos.line;

  bool rangeEndsAtLineEnd = range.isValid()
      && !range.isEmpty()
      && range.end.col >= static_cast<int>(preDelLines[range.end.line].size());

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

KeyedSequence ChangeGoalHandler::buildChangePrefix(
    const SequenceBinding& sourceCmd,
    const Lines& postDelLines, const CursorPos& postDelPos,
    const Lines& preDelLines, const CharLineRange& range) const {
  int totalLines = static_cast<int>(postDelLines.size());
  int cursorLine = postDelPos.line;

  if (range.begin.col == 0) {
    int removedLines = range.lineCountTouched();
    if (static_cast<int>(preDelLines.size()) - removedLines > 0) {
      totalLines++;
      cursorLine++;
    }
  }

  KeyedSequence prefix = deleteToChangeChar(sourceCmd);
  prefix += buildCollapseSequence(totalLines, cursorLine);
  return prefix;
}

KeyedSequence ChangeGoalHandler::buildChangePrefix(
    const SequenceBinding& sourceCmd,
    const Lines& postDelLines, const CursorPos& postDelPos,
    const Lines& preDelLines, const LineCharRange& range) const {
  int totalLines = static_cast<int>(postDelLines.size());
  int cursorLine = postDelPos.line;

  bool rangeEndsAtLineEnd =
      range.end.col >= static_cast<int>(preDelLines[range.end.line].size());
  if (range.end.line > range.beginLine &&
      rangeEndsAtLineEnd &&
      static_cast<int>(preDelLines.size()) - range.lineCountTouched() > 0) {
    totalLines++;
    cursorLine++;
  }

  KeyedSequence prefix = deleteToChangeChar(sourceCmd);
  prefix += buildCollapseSequence(totalLines, cursorLine);
  return prefix;
}

// Suffix cache methods

vector<KeyedSequence> ChangeGoalHandler::buildKeyedSequencesFromParsedEdits(
    const vector<ParsedEdit>& edits) const {
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

vector<RunningEffort> ChangeGoalHandler::buildRawSuffixEfforts(
    const SuffixProgram& program,
    const RunningEffort& terminalSuffixEffort,
    int extraPenaltyIndex,
    double extraPenalty) const {
  int n = program.size();
  vector<RunningEffort> suffixEfforts(n + 1);
  suffixEfforts[n] = terminalSuffixEffort;

  for (int i = n - 1; i >= 0; i--) {
    RunningEffort editEffort(program.editCmds[i].keys, config);
    if (i == extraPenaltyIndex && extraPenalty > 0.0) {
      editEffort.addPenalty(extraPenalty);
    }
    suffixEfforts[i] = RunningEffort::merge(editEffort, suffixEfforts[i + 1]);
  }
  return suffixEfforts;
}

SuffixValue ChangeGoalHandler::buildSuffixValueForNextIndex(
    const shared_ptr<const SuffixProgram>& suffixProgram,
    const vector<ParsedEdit>& dotAwareEdits,
    const vector<RunningEffort>& rawSuffixEfforts,
    int nextIndex,
    const string& lastEditCmd) const {
  int programSize = suffixProgram->size();
  assert(nextIndex >= 0 && nextIndex <= programSize);

  if (nextIndex >= static_cast<int>(dotAwareEdits.size()) ||
      dotAwareEdits[nextIndex].edit != "." || lastEditCmd.empty()) {
    return SuffixValue(suffixProgram, nextIndex, rawSuffixEfforts[nextIndex]);
  }

  KeyedSequence expandedPrefix(lastEditCmd, globalSequenceToKeys().tokenize(lastEditCmd));
  RunningEffort expandedPrefixEffort(expandedPrefix.keys, config);

  RunningEffort expandedEffort =
      RunningEffort::merge(expandedPrefixEffort, rawSuffixEfforts[nextIndex + 1]);

  return SuffixValue(
      suffixProgram, nextIndex + 1, std::move(expandedPrefix), expandedEffort,
      lastEditCmd, nextIndex, rawSuffixEfforts[nextIndex]);
}

void ChangeGoalHandler::replayAndCacheSuffix(
    int startIndex, const string& searchPrefixSeq,
    const KeyedSequence& completionSuffix,
    double completionPenalty,
    const KeyedSequence& typedSuffix,
    const RunningEffort& typedSuffixEffort) {
  cachePopulations++;

  vector<ParsedEdit> prefixEdits = Edit::parseEdits(searchPrefixSeq);
  int prefixEditCount = static_cast<int>(prefixEdits.size());
  vector<KeyedSequence> replayEditCmds = buildKeyedSequencesFromParsedEdits(prefixEdits);
  replayEditCmds.push_back(completionSuffix);
  int programEditCount = static_cast<int>(replayEditCmds.size());
  auto suffixProgram = make_shared<SuffixProgram>(std::move(replayEditCmds), typedSuffix);

  vector<RunningEffort> rawSuffixEfforts =
      buildRawSuffixEfforts(*suffixProgram, typedSuffixEffort,
                            prefixEditCount, completionPenalty);

  Lines replayLines = effectiveLines;
  CursorPos replayPos = seedPositionForStart(startIndex, initialLines, leftColOffset);
  Mode replayMode = Mode::Normal;

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
                    transformBoundary.hasLinesBelow(),
                    leftColOffset, rightColOffset,
                    transformBoundary.hasLinesAbove());
  }
}

optional<Result> ChangeGoalHandler::tryReplacement(
    string_view deleted, string_view inserted,
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

  if (diff[0] > 0) appendNav(diff[0]);

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

  int lastDiff = diff.back();
  int endPos = static_cast<int>(inserted.size()) - 1;
  if (lastDiff < endPos) appendNav(endPos - lastDiff);

  RunningEffort effort(ks.keys, config);
  double totalEffort = effort.getEffort(config);
  if (totalEffort > maxEffort) return nullopt;

  return Result(std::move(ks.seq), totalEffort);
}

// Goal emission methods

GoalStates ChangeGoalHandler::emitEditGoal(
    const TransformEditorState& postCompletionState, const TransformState& base,
    const SequenceBinding& sourceCmd,
    const KeyedSequence& goalCompletionCmd,
    const TypedGoalVariants& typedVariants,
    bool allowDotGoalPath) {
  double completionPenalty = sourceCmd.effort.getPenalty();

  debug("emitEditGoal:",
        "sourceBase='" + string(sourceCmd.base.seq.view()) + "'",
        "sourceCount=" + to_string(sourceCmd.count),
        "goalCompletion='" + goalCompletionCmd.seq.str() + "'",
        "typed='" + typedVariants.normalTyped.sequence.seq.str() + "'",
        "baseSeq='" + base.getSeq() + "'",
        "postPos=(" + to_string(postCompletionState.getPos().line) + "," +
            to_string(postCompletionState.getPos().col) + ")");

  RunningEffort normalEffort = mergeGoalSuffixEffort(
      goalCompletionCmd, typedVariants.normalTyped.effort, completionPenalty, config);
  KeyedSequence normalSeq = goalCompletionCmd;
  normalSeq += typedVariants.normalTyped.sequence;

  debug("emitEditGoal: fullNormalSeq='" + normalSeq.seq.str() + "'");

  replayAndCacheSuffix(base.getStartIndex(), base.getSeq(),
                       goalCompletionCmd, completionPenalty,
                       typedVariants.normalTyped.sequence, typedVariants.normalTyped.effort);

  TransformStateFactory states(config, effortWeight);
  TransformState normalState = states.afterCommandWithLastEdit(
      base, postCompletionState, normalSeq.seq.view(), normalEffort, 0.0,
      sourceCmd.count, sourceCmd.base.seq.view());

  GoalStates result{std::move(normalState), nullopt};

  if (allowDotGoalPath) {
    KeyedSequence dotSeq(".", KeyedSequence::Period.keys);
    dotSeq += KeyedSequence::i;
    dotSeq += buildCollapseSequence(
        static_cast<int>(postCompletionState.getLines().size()),
        postCompletionState.getPos().line);
    RunningEffort dotPrefixEffort(dotSeq.keys, config);
    RunningEffort dotEffort = RunningEffort::merge(dotPrefixEffort, typedVariants.dotTyped.effort);
    dotSeq += typedVariants.dotTyped.sequence;

    result.dotVariant = states.afterCommand(
        base, postCompletionState, dotSeq.seq.view(), dotEffort, 0.0);
  }

  return result;
}

GoalStates ChangeGoalHandler::emitLinewiseChangeGoal(
    const TransformEditorState& afterDel, const TransformState& base,
    const SequenceBinding& sourceCmd, int line,
    int ccLineCount, const KeyedSequence& changeCmd,
    bool applyAutoindent) {
  bool isDot = isDotRepeat(base, sourceCmd);
  int autoindentLen = 0;
  if constexpr (VimOptions::autoindent()) {
    if (applyAutoindent) {
      autoindentLen = leadingSpaceCount(base.getLines()[line]);
    }
  }

  KeyedSequence changePrefix = changeCmd;
  bool useAfterIndent = false;
  bool bsInCollapse = (ccLineCount > 1 && line > 0);
  if constexpr (VimOptions::autoindent()) {
    if (applyAutoindent && !bsInCollapse) {
      changePrefix += computeIndentAdjustment(autoindentLen, goalFirstIndentLen);
      changePrefix += buildCollapseSequence(ccLineCount, line);
      useAfterIndent = goalFirstIndentLen > 0;
    } else if (applyAutoindent && bsInCollapse) {
      int bsClear = autoindentLen > 0 ? bsCountForIndent(autoindentLen, 0) : 0;
      changePrefix.append(KeyedSequence::BS, bsClear + line);
      changePrefix.append(KeyedSequence::Del, ccLineCount - 1 - line);
    } else {
      changePrefix += buildCollapseSequence(ccLineCount, line);
    }
  } else {
    changePrefix += buildCollapseSequence(ccLineCount, line);
  }

  const auto& suffixTyped = useAfterIndent ? typedAfterIndent : typed;
  const auto& suffixEffort = useAfterIndent ? afterIndentEffort : typedEffort;
  TypedGoalVariants typedVariants{{suffixTyped, suffixEffort}, {typed, typedEffort}};
  return emitEditGoal(afterDel, base, sourceCmd, changePrefix, typedVariants, isDot);
}

GoalStates ChangeGoalHandler::onLinewiseGoal(
    const TransformEditorState& afterDel, const TransformState& base, LineRange range,
    const SequenceBinding& sourceCmd) {
  int line = range.beginLine;
  KeyedSequence changeCmd = deleteToChangeLine(sourceCmd, base.getLines()[line]);
  bool applyAutoindent = false;
  if constexpr (VimOptions::autoindent()) {
    applyAutoindent = (changeCmd.seq.view() != "0C");
  }
  return emitLinewiseChangeGoal(afterDel, base, sourceCmd, line,
                                static_cast<int>(base.getLines().size()),
                                changeCmd, applyAutoindent);
}

GoalStates ChangeGoalHandler::onCountedLinewiseGoal(
    const TransformEditorState& afterDel, const TransformState& base,
    LineRange range, const SequenceBinding& sourceCmd) {
  int line = range.beginLine;
  int lineCount = range.endLine - range.beginLine;
  int ccLineCount = static_cast<int>(base.getLines().size()) - lineCount + 1;
  KeyedSequence changeCmd = deleteToChangeLine(sourceCmd, base.getLines()[line]);
  return emitLinewiseChangeGoal(afterDel, base, sourceCmd, line, ccLineCount, changeCmd, true);
}

GoalStates ChangeGoalHandler::onJoinGoal(
    const TransformEditorState& afterJn, const TransformState& base,
    const SequenceBinding& sourceCmd) {
  bool isDot = isDotRepeat(base, sourceCmd);
  const auto& iCmd = KeyedSequence::i;
  KeyedSequence goalCompletionCmd;
  appendOptionalCount(goalCompletionCmd, sourceCmd.count, sourceCmd.base);
  goalCompletionCmd += iCmd;
  goalCompletionCmd += buildCollapseSequence(
      static_cast<int>(afterJn.getLines().size()), afterJn.getPos().line);
  TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
  return emitEditGoal(afterJn, base, sourceCmd, goalCompletionCmd, typedVariants, isDot);
}

SuffixCacheResult ChangeGoalHandler::tryUseSuffixCache(
    const TransformState& s,
    vector<vector<Result>>& resultsByStart,
    int maxResultsPerStart,
    const vector<char>& startActive,
    int& resultsFound,
    int& uniquePositionsCovered) {
  int idx = s.getStartIndex();
  if (!startActive[idx]) return {};

  SuffixKey sk(s.getLinesHash(), static_cast<int>(s.getLines().size()), s.getPos(), s.getMode());
  auto cacheIt = suffixCache.find(sk);
  if (cacheIt == suffixCache.end()) return {};

  cacheHits++;
  auto& bucket = resultsByStart[static_cast<size_t>(idx)];
  if (static_cast<int>(bucket.size()) >= maxResultsPerStart) return {true, false};

  const SuffixValue& sv = cacheIt->second;

  bool useDot = sv.canUseDot(s.getLastEditCount(), s.getLastEditBase());
  const KeyedSequence& suffix = sv.suffix(useDot);
  const RunningEffort& suffixEffort = sv.suffixEffort(useDot);

  string seqStr = s.getSeq() + suffix.seq.str();
  debug("suffixCache hit:",
        "prefixSeq='" + s.getSeq() + "'",
        "suffix='" + suffix.seq.str() + "'",
        "fullSeq='" + seqStr + "'",
        "pos=(" + to_string(s.getPos().line) + "," + to_string(s.getPos().col) + ")",
        "useDot=" + string(useDot ? "true" : "false"));
  RunningEffort mergedEffort = RunningEffort::merge(s.getRunningEffort(), suffixEffort);
  double totalEffort = mergedEffort.getEffort(config);

  bool firstForStart = bucket.empty();
  bucket.emplace_back(seqStr, totalEffort);
  resultsFound++;
  if (firstForStart) uniquePositionsCovered++;
  bool capped = (static_cast<int>(bucket.size()) == maxResultsPerStart);
  return {true, capped};
}

TransformResult ChangeGoalHandler::finalize(
    vector<vector<Result>>&& resultsByStart, const Lines& initialLines,
    const Lines& goalLines, const TransformOptimizerParams&,
    int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
    TransformSearchStats stats) {
  auto& bucket = resultsByStart[0];
  if (!bucket.empty() &&
      initialLines.size() == 1 && goalLines.size() == 1 &&
      initialLines[0].size() == goalLines[0].size()) {
    double bestCost = min_element(bucket.begin(), bucket.end(),
        [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); })->getCost();
    auto replacementResult = tryReplacement(initialLines[0], goalLines[0],
                                            config, bestCost);
    if (replacementResult.has_value()) {
      bucket.push_back(*replacementResult);
    }
  }

  stats.setCacheStats(cacheHits,
                      static_cast<int>(suffixCache.size()),
                      cachePopulations);

  return TransformResult(std::move(resultsByStart), stats, initialLines,
                    bufferBeginLine, bufferBeginCol, goalPos);
}
