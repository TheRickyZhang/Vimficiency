#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TransformState.h"
#include "GoalHandlerTypes.h"
#include "SuffixCache.h"

#include "Boundary/TransformBoundary.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"
#include "Optimizer/Result.h"
#include "Optimizer/SequenceBinding.h"
#include "Types/CharLineRange.h"
#include "Types/CharRange.h"
#include "Types/CursorPos.h"
#include "Types/LineCharRange.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"

struct TransformOptimizerParams;
struct TransformResult;
struct TransformSearchStats;

struct ChangeGoalHandler {
  const Lines& effectiveLines;
  int leftColOffset;
  int rightColOffset;
  double effortWeight;
  const Config& config;
  const TransformBoundary& transformBoundary;
  const Lines& initialLines;
  const Lines& goalLines;
  const std::string& pre;
  const std::string& suf;
  const std::string& preSuf;

  KeyedSequence typed;
  RunningEffort typedEffort;
  SuffixCacheMap suffixCache;
  int cacheHits = 0;
  int cachePopulations = 0;

  int goalFirstIndentLen = 0;
  KeyedSequence typedAfterIndent;
  RunningEffort afterIndentEffort;

  ChangeGoalHandler(const Lines& effectiveLines, int leftColOffset, int rightColOffset,
                    double effortWeight, const Config& config,
                    const TransformBoundary& transformBoundary, const Lines& initialLines,
                    const Lines& goalLines, const std::string& pre,
                    const std::string& suf, const std::string& preSuf);

  bool isGoalReached(const Lines& lines) const;
  bool isGoalReached(const TransformEditorState& state) const;

  SuffixCacheResult tryUseSuffixCache(const TransformState& s,
                                      std::vector<std::vector<Result>>& resultsByStart,
                                      int maxResultsPerStart,
                                      const std::vector<char>& startActive,
                                      int& resultsFound,
                                      int& uniquePositionsCovered);

  TransformResult finalize(std::vector<std::vector<Result>>&& resultsByStart,
                      const Lines& initialLines, const Lines& goalLines,
                      const TransformOptimizerParams& params,
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
                      TransformSearchStats stats,
                      std::vector<std::vector<CursorPos>>&& resultGoalPositions);

  Result resultFromClearedGoal(const TransformState& base) const;

  // Goal emission methods — return states for the dispatcher to emit.
  // Only called in change mode (not pure deletion).
  template<class RangeT>
  GoalStates onDeletionGoal(const TransformEditorState& afterDel, const TransformState& base,
                            const RangeT& range, const SequenceBinding& sourceCmd);

  GoalStates onLinewiseGoal(const TransformEditorState& afterDel, const TransformState& base,
                            LineRange range, const SequenceBinding& sourceCmd);

  GoalStates onCountedLinewiseGoal(const TransformEditorState& afterDel, const TransformState& base,
                                   LineRange range, const SequenceBinding& sourceCmd);

  GoalStates onJoinGoal(const TransformEditorState& afterJn, const TransformState& base,
                        const SequenceBinding& sourceCmd);

  // Stateless command-form helpers — exposed so depth-1 frontiers
  // (TransformFrontier::enumerateDepth1DeletionStructurals) can convert
  // a deletion enumerated by TransformExplorer into its change-mode
  // analog without re-running the goal handler. `deleteToChangeChar`
  // covers `D`→`C`, `x`→`s`, `X`→`hs`, `dw`→`dwi`, generic `dX`→`cX`.
  // `deleteToChangeLine` covers `dd`→`cc` (or `0C` under autoindent
  // with leading whitespace) and counted variants.
  static KeyedSequence deleteToChangeChar(const SequenceBinding& sourceCmd);
  static KeyedSequence deleteToChangeLine(const SequenceBinding& sourceCmd,
                                          std::string_view lineContent);

  // Cleared-shell collapse helpers — exposed so primitive property tests can
  // verify the entry+collapse contract directly against the oracle. See the
  // implementation in ChangeGoalHandler.cpp for the cleared-shell invariant.
  static KeyedSequence buildCollapseSequence(int totalLines, int cursorLine);
  static KeyedSequence buildClearedShellEntry(int totalLines, int cursorLine, int cursorCol);

  struct SuffixCommandInfo {
    bool isDotRepeat = false;
    bool updatesDotRepeat = false;
  };

private:
  struct KeyedSegment {
    const KeyedSequence& sequence;
    const RunningEffort& effort;
  };

  struct TypedGoalVariants {
    KeyedSegment normalTyped;
    KeyedSegment dotTyped;
  };

  bool isDotRepeat(const TransformState& base, const SequenceBinding& sourceCmd) const;

  // Static helpers
  static void appendOptionalCount(KeyedSequence& out, int count, const KeyedSequence& base);
  static KeyedSequence withOptionalCount(int count, const KeyedSequence& base);
  static std::string formatCountedCommand(int count, std::string_view baseCmd);
  static RunningEffort mergeGoalSuffixEffort(const KeyedSequence& prefix,
                                             const RunningEffort& typedSuffixEffort,
                                             double completionPenalty,
                                             const Config& config);

  // Build change prefix overloads
  KeyedSequence buildChangePrefix(const SequenceBinding& sourceCmd,
                                  const Lines& postDelLines, const CursorPos& postDelPos,
                                  const Lines& preDelLines, const CharRange& range) const;
  KeyedSequence buildChangePrefix(const SequenceBinding& sourceCmd,
                                  const Lines& postDelLines, const CursorPos& postDelPos,
                                  const Lines& preDelLines, const CharLineRange& range) const;
  KeyedSequence buildChangePrefix(const SequenceBinding& sourceCmd,
                                  const Lines& postDelLines, const CursorPos& postDelPos,
                                  const Lines& preDelLines, const LineCharRange& range) const;

  // Suffix cache helpers
  std::vector<RunningEffort> buildRawSuffixEfforts(const SuffixProgram& program,
                                                   const RunningEffort& terminalSuffixEffort,
                                                   int extraPenaltyIndex,
                                                   double extraPenalty) const;
  SuffixValue buildSuffixValueForNextIndex(
      const std::shared_ptr<const SuffixProgram>& suffixProgram,
      const std::vector<SuffixCommandInfo>& commandInfos,
      const std::vector<RunningEffort>& rawSuffixEfforts,
      int nextIndex,
      const DotRepeat& lastEdit) const;
  void cacheSuffixesForPath(const TransformState& base,
                            const KeyedSequence& completionSuffix,
                            double completionPenalty,
                            const KeyedSequence& typedSuffix,
                            const RunningEffort& typedSuffixEffort);

  // Core goal emission — returns states instead of calling back.
  GoalStates emitEditGoal(const TransformEditorState& postCompletionState,
                          const TransformState& base,
                          const SequenceBinding& sourceCmd,
                          const KeyedSequence& goalCompletionCmd,
                          const TypedGoalVariants& typedVariants,
                          bool allowDotGoalPath);

  GoalStates emitLinewiseChangeGoal(const TransformEditorState& afterDel,
                                    const TransformState& base,
                                    const SequenceBinding& sourceCmd, int line,
                                    int ccLineCount, const KeyedSequence& changeCmd,
                                    bool applyAutoindent,
                                    bool collapseLines = true);

  static CursorPos seedPositionForStart(int startIndex, const Lines& initialLines, int leftColOffset);
};

// onDeletionGoal is the only template — stays in header (varies on RangeT).
template<class RangeT>
GoalStates ChangeGoalHandler::onDeletionGoal(
    const TransformEditorState& afterDel, const TransformState& base, const RangeT& range,
    const SequenceBinding& sourceCmd) {
  bool isDot = isDotRepeat(base, sourceCmd);
  KeyedSequence goalCompletionCmd = buildChangePrefix(sourceCmd,
                                                      afterDel.getLines(), afterDel.getPos(),
                                                      base.getLines(), range);
  TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
  return emitEditGoal(afterDel, base, sourceCmd, goalCompletionCmd, typedVariants, isDot);
}
