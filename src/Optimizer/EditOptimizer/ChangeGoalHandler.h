#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "EditState.h"
#include "GoalHandlerTypes.h"
#include "SuffixCache.h"

#include "Boundary/EditBoundary.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/EditInterpreter.h"
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

struct EditOptimizerParams;
struct EditResult;
struct EditSearchStats;

struct ChangeGoalHandler {
  const Lines& effectiveLines;
  int leftColOffset;
  int rightColOffset;
  double effortWeight;
  const Config& config;
  const EditBoundary& editBoundary;
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
                    const EditBoundary& editBoundary, const Lines& initialLines,
                    const Lines& goalLines, const std::string& pre,
                    const std::string& suf, const std::string& preSuf);

  bool isGoalReached(const Lines& lines) const;

  SuffixCacheResult tryUseSuffixCache(const EditState& s,
                                      std::vector<std::vector<Result>>& resultsByStart,
                                      int maxResultsPerStart,
                                      const std::vector<char>& startActive,
                                      int& resultsFound,
                                      int& uniquePositionsCovered);

  EditResult finalize(std::vector<std::vector<Result>>&& resultsByStart,
                      const Lines& initialLines, const Lines& goalLines,
                      const EditOptimizerParams& params,
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
                      EditSearchStats stats);

  // Goal emission methods — return states for the dispatcher to emit.
  // Only called in change mode (not pure deletion).
  template<class RangeT>
  GoalStates onDeletionGoal(EditState& afterDel, const EditState& base, const RangeT& range,
                            const SequenceBinding& sourceCmd);

  GoalStates onLinewiseGoal(EditState& afterDel, const EditState& base, LineRange range,
                            const SequenceBinding& sourceCmd);

  GoalStates onCountedLinewiseGoal(EditState& afterDel, const EditState& base,
                                   LineRange range, const SequenceBinding& sourceCmd);

  GoalStates onJoinGoal(EditState& afterJn, const EditState& base,
                        const SequenceBinding& sourceCmd);

private:
  struct KeyedSegment {
    const KeyedSequence& sequence;
    const RunningEffort& effort;
  };

  struct TypedGoalVariants {
    KeyedSegment normalTyped;
    KeyedSegment dotTyped;
  };

  bool isDotRepeat(const EditState& base, const SequenceBinding& sourceCmd) const;

  // Static helpers
  static KeyedSequence buildCollapseSequence(int totalLines, int cursorLine);
  static void appendOptionalCount(KeyedSequence& out, int count, const KeyedSequence& base);
  static KeyedSequence withOptionalCount(int count, const KeyedSequence& base);
  static KeyedSequence deleteToChangeChar(const SequenceBinding& sourceCmd);
  static KeyedSequence deleteToChangeLine(const SequenceBinding& sourceCmd,
                                          std::string_view lineContent);
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
  std::vector<KeyedSequence> buildKeyedSequencesFromParsedEdits(
      const std::vector<ParsedEdit>& edits) const;
  std::vector<RunningEffort> buildRawSuffixEfforts(const SuffixProgram& program,
                                                   const RunningEffort& terminalSuffixEffort,
                                                   int extraPenaltyIndex,
                                                   double extraPenalty) const;
  SuffixValue buildSuffixValueForNextIndex(
      const std::shared_ptr<const SuffixProgram>& suffixProgram,
      const std::vector<ParsedEdit>& dotAwareEdits,
      const std::vector<RunningEffort>& rawSuffixEfforts,
      int nextIndex,
      const std::string& lastEditCmd) const;
  void replayAndCacheSuffix(int startIndex, const std::string& searchPrefixSeq,
                            const KeyedSequence& completionSuffix,
                            double completionPenalty,
                            const KeyedSequence& typedSuffix,
                            const RunningEffort& typedSuffixEffort);

  // Core goal emission — returns states instead of calling back.
  GoalStates emitEditGoal(EditState& postCompletionState, const EditState& base,
                          const SequenceBinding& sourceCmd,
                          const KeyedSequence& goalCompletionCmd,
                          const TypedGoalVariants& typedVariants,
                          bool allowDotGoalPath);

  GoalStates emitLinewiseChangeGoal(EditState& afterDel, const EditState& base,
                                    const SequenceBinding& sourceCmd, int line,
                                    int ccLineCount, const KeyedSequence& changeCmd,
                                    bool applyAutoindent);

  // Replacement strategy
  static std::optional<Result> tryReplacement(std::string_view deleted, std::string_view inserted,
                                              const Config& config, double maxEffort);

  static CursorPos seedPositionForStart(int startIndex, const Lines& initialLines, int leftColOffset);
};

// onDeletionGoal is the only template — stays in header (varies on RangeT).
template<class RangeT>
GoalStates ChangeGoalHandler::onDeletionGoal(
    EditState& afterDel, const EditState& base, const RangeT& range,
    const SequenceBinding& sourceCmd) {
  bool isDot = isDotRepeat(base, sourceCmd);
  KeyedSequence goalCompletionCmd = buildChangePrefix(sourceCmd,
                                                      afterDel.getLines(), afterDel.getPos(),
                                                      base.getLines(), range);
  TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
  return emitEditGoal(afterDel, base, sourceCmd, goalCompletionCmd, typedVariants, isDot);
}
