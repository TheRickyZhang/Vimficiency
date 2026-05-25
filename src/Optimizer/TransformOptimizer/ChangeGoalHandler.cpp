#include "ChangeGoalHandler.h"

#include <algorithm>
#include <cassert>

#include "TransformOptimizer.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/TransformOptimizer/TransformPostExplorerEmissions.h"
#include "Utils/Debug.h"

using namespace std;

namespace {

int firstDotBeforeDotContextReset(
    const vector<ChangeGoalHandler::SuffixCommandInfo>& commands,
    int start) {
  for (int i = start; i < static_cast<int>(commands.size()); i++) {
    if (commands[i].isDotRepeat) return i;
    if (commands[i].updatesDotRepeat) return -1;
  }
  return -1;
}

vector<const TransformPathStep*> collectPath(
    shared_ptr<const TransformPathStep> tail) {
  vector<const TransformPathStep*> path;
  for (auto node = std::move(tail); node; node = node->prev) {
    path.push_back(node.get());
  }
  reverse(path.begin(), path.end());
  return path;
}

KeyedSequence buildExpandedDotPrefix(const SuffixProgram& program,
                                     int startIndex,
                                     int dotIndex,
                                     string_view expandedDotCmd) {
  KeyedSequence prefix;
  for (int i = startIndex; i < dotIndex; i++) {
    prefix += program.editCmds[i];
  }
  prefix += KeyedSequence(
      expandedDotCmd, globalSequenceToKeys().tokenize(expandedDotCmd));
  return prefix;
}

struct LinewiseChangeShape {
  int lineCount;
  int cursorLine;
};

LinewiseChangeShape linewiseChangeShapeForCommand(
    const TransformState& base,
    LineRange range,
    string_view baseCmd) {
  int rangeLineCount = range.endLine - range.beginLine;
  // `c}` at EOF is characterwise: preserved lines above the cursor remain
  // above the insert line, so only those line breaks can need `<BS>`.
  if (baseCmd == "d}" &&
      range.endLine == static_cast<int>(base.getLines().size())) {
    return {range.beginLine + 1, range.beginLine};
  }
  return {
      static_cast<int>(base.getLines().size()) - rangeLineCount + 1,
      range.beginLine};
}

}  // namespace

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

string ChangeGoalHandler::formatCountedCommand(int count, string_view baseCmd) {
  if (count <= 0) return string(baseCmd);
  return to_string(count) + string(baseCmd);
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
    const vector<SuffixCommandInfo>& commandInfos,
    const vector<RunningEffort>& rawSuffixEfforts,
    int nextIndex,
    int lastEditCount,
    string_view lastEditBase) const {
  int programSize = suffixProgram->size();
  assert(nextIndex >= 0 && nextIndex <= programSize);

  int dotIndex = firstDotBeforeDotContextReset(commandInfos, nextIndex);
  if (dotIndex < 0 || lastEditBase.empty()) {
    return SuffixValue(suffixProgram, nextIndex, rawSuffixEfforts[nextIndex]);
  }

  string expectedDotCmd = formatCountedCommand(lastEditCount, lastEditBase);
  KeyedSequence expandedPrefix = buildExpandedDotPrefix(
      *suffixProgram, nextIndex, dotIndex, expectedDotCmd);
  RunningEffort expandedPrefixEffort(expandedPrefix.keys, config);

  RunningEffort expandedEffort =
      RunningEffort::merge(expandedPrefixEffort, rawSuffixEfforts[dotIndex + 1]);

  return SuffixValue(
      suffixProgram, dotIndex + 1, std::move(expandedPrefix), expandedEffort,
      expectedDotCmd, nextIndex, rawSuffixEfforts[nextIndex]);
}

void ChangeGoalHandler::cacheSuffixesForPath(
    const TransformState& base,
    const KeyedSequence& completionSuffix,
    double completionPenalty,
    const KeyedSequence& typedSuffix,
    const RunningEffort& typedSuffixEffort) {
  // Suffix cache is populated only from completed Normal-mode goal paths.
  // The path-step chain's initial state is the search start (no preceding
  // command), which is always Normal.
  CHECK(base.getMode() == Mode::Normal,
        "cacheSuffixesForPath called from a non-Normal base state");
  cachePopulations++;

  vector<const TransformPathStep*> path = collectPath(base.getPathTail());
  vector<KeyedSequence> commands;
  vector<SuffixCommandInfo> commandInfos;
  commands.reserve(path.size() + 1);
  commandInfos.reserve(path.size() + 1);
  for (const TransformPathStep* step : path) {
    commands.emplace_back(
        step->command, globalSequenceToKeys().tokenize(step->command));
    commandInfos.push_back(SuffixCommandInfo{
        step->isDotRepeat, step->updatesDotRepeat});
  }
  commands.push_back(completionSuffix);
  commandInfos.push_back(SuffixCommandInfo{
      false, true});

  int prefixCommandCount = static_cast<int>(path.size());
  int programCommandCount = static_cast<int>(commands.size());
  auto suffixProgram = make_shared<SuffixProgram>(std::move(commands), typedSuffix);

  vector<RunningEffort> rawSuffixEfforts =
      buildRawSuffixEfforts(*suffixProgram, typedSuffixEffort,
                            prefixCommandCount, completionPenalty);

  size_t initialHash = hashLines(effectiveLines);
  int initialLineCount = static_cast<int>(effectiveLines.size());
  CursorPos initialPos =
      seedPositionForStart(base.getStartIndex(), initialLines, leftColOffset);
  for (int nextIndex = 0; nextIndex < programCommandCount; nextIndex++) {
    size_t linesHash = initialHash;
    int lineCount = initialLineCount;
    CursorPos replayPos = initialPos;
    Mode replayMode = base.getMode();
    int lastEditCount = 0;
    string_view lastEditBase;
    if (nextIndex > 0) {
      const TransformPathStep* previous = path[static_cast<size_t>(nextIndex - 1)];
      linesHash = previous->linesHashAfter;
      lineCount = previous->lineCountAfter;
      replayPos = previous->posAfter;
      replayMode = previous->modeAfter;
      lastEditCount = previous->lastEditCountAfter;
      lastEditBase = previous->lastEditBaseAfter;
    }

    SuffixKey sk(linesHash, lineCount, replayPos, replayMode);
    if (suffixCache.find(sk) == suffixCache.end()) {
      suffixCache[sk] = buildSuffixValueForNextIndex(
          suffixProgram, commandInfos, rawSuffixEfforts, nextIndex,
          lastEditCount, lastEditBase);
    }
  }
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

  cacheSuffixesForPath(base, goalCompletionCmd, completionPenalty,
                       typedVariants.normalTyped.sequence,
                       typedVariants.normalTyped.effort);

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
    bool applyAutoindent, bool collapseLines) {
  bool isDot = isDotRepeat(base, sourceCmd);
  int autoindentLen = 0;
  if constexpr (VimOptions::autoindent()) {
    if (applyAutoindent) {
      autoindentLen = leadingSpaceCount(base.getLines()[line]);
    }
  }

  KeyedSequence changePrefix = changeCmd;
  bool useAfterIndent = false;
  bool bsInCollapse = collapseLines && ccLineCount > 1 && line > 0;
  if constexpr (VimOptions::autoindent()) {
    if (!collapseLines) {
      useAfterIndent = false;
    } else if (applyAutoindent && !bsInCollapse) {
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
    if (collapseLines) {
      changePrefix += buildCollapseSequence(ccLineCount, line);
    }
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
  bool paragraphChangeAtEof =
      sourceCmd.base.seq.view() == "d}" &&
      range.endLine == static_cast<int>(base.getLines().size());
  LinewiseChangeShape shape =
      linewiseChangeShapeForCommand(base, range, sourceCmd.base.seq.view());
  bool collapseLines = paragraphChangeAtEof ? line > 0 : true;
  return emitLinewiseChangeGoal(afterDel, base, sourceCmd, shape.cursorLine,
                                shape.lineCount, changeCmd, applyAutoindent,
                                collapseLines);
}

GoalStates ChangeGoalHandler::onCountedLinewiseGoal(
    const TransformEditorState& afterDel, const TransformState& base,
    LineRange range, const SequenceBinding& sourceCmd) {
  int line = range.beginLine;
  KeyedSequence changeCmd = deleteToChangeLine(sourceCmd, base.getLines()[line]);
  bool paragraphChangeAtEof =
      sourceCmd.base.seq.view() == "d}" &&
      range.endLine == static_cast<int>(base.getLines().size());
  LinewiseChangeShape shape =
      linewiseChangeShapeForCommand(base, range, sourceCmd.base.seq.view());
  return emitLinewiseChangeGoal(
      afterDel, base, sourceCmd, shape.cursorLine, shape.lineCount, changeCmd, true,
      paragraphChangeAtEof ? line > 0 : true);
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

  SuffixKey sk(
      s.getLinesHash(), static_cast<int>(s.getLines().size()), s.getPos(),
      s.getMode());
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
    TransformSearchStats stats,
    vector<vector<CursorPos>>&& resultGoalPositions) {
  auto& bucket = resultsByStart[0];
  if (!bucket.empty() &&
      initialLines.size() == 1 && goalLines.size() == 1 &&
      initialLines[0].size() == goalLines[0].size()) {
    double bestCost = min_element(bucket.begin(), bucket.end(),
        [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); })->getCost();
    auto replacementResult = TransformPostExplorer::tryReplacement(
        initialLines[0], goalLines[0], config, bestCost);
    if (replacementResult.has_value()) {
      bucket.push_back(*replacementResult);
    }
  }

  stats.setCacheStats(cacheHits,
                      static_cast<int>(suffixCache.size()),
                      cachePopulations);

  return TransformResult(
      std::move(resultsByStart), stats, initialLines,
      bufferBeginLine, bufferBeginCol, goalPos, std::move(resultGoalPositions));
}
