#pragma once

#include <concepts>
#include <queue>
#include <unordered_map>
#include <vector>

#include "EditState.h"
#include "GoalHandlerTypes.h"

#include "Optimizer/Result.h"
#include "Optimizer/SearchFrontier.h"
#include "Optimizer/SearchStats.h"
#include "Optimizer/SequenceBinding.h"

#include "Effort/EffortBank.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"
#include "Types/CharLineRange.h"
#include "Types/CharRange.h"
#include "Types/LineCharRange.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"
#include "Boundary/EditBoundary.h"

struct EditStateComparator {
  bool operator()(const EditState& a, const EditState& b) const {
    double aPriority = a.getCost();
    double bPriority = b.getCost();
    if (aPriority != bPriority) return aPriority > bPriority;
    return a.getStartIndex() > b.getStartIndex();
  }
};

using EditPriorityQueue = std::priority_queue<EditState, std::vector<EditState>, EditStateComparator>;
using EditCostMap = std::unordered_map<EditStateKey, double, EditStateKeyHash>;

inline double weightedHeuristicCost(const Lines& lines, int leftColOffset, int rightColOffset,
                                    double distanceWeight) {
  int total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (i == 0) lineLen -= leftColOffset;
    if (i == lines.size() - 1) lineLen -= rightColOffset;
    total += std::max(0, lineLen);
  }
  total += (static_cast<int>(lines.size()) - 1) * 2;
  return distanceWeight * static_cast<double>(total);
}

inline EditState applyDeletionTransition(const EditState& base, const CharRange& range,
                                         std::string_view baseCmd) {
  bool isBackwardWordDelete = (baseCmd == "db" || baseCmd == "dB");
  return isBackwardWordDelete
      ? base.afterBackwardWordDeletion(range)
      : base.afterDeletion(range);
}

inline EditState applyDeletionTransition(const EditState& base, const CharLineRange& range,
                                         std::string_view) {
  return base.afterCharLineDeletion(range);
}

inline EditState applyDeletionTransition(const EditState& base, const LineCharRange& range,
                                         std::string_view) {
  return base.afterLineCharDeletion(range);
}

// Forward declarations for concept signatures — avoids pulling in EditOptimizer.h.
struct EditOptimizerParams;
struct EditResult;

// GoalHandlerCore: the shared contract between DeletionGoalHandler and
// ChangeGoalHandler.  Both are consumed by optimizeImpl and
// EditTransitionDispatcher.
//
//   isGoalReached     — test whether the buffer has reached the goal state
//   tryUseSuffixCache — attempt suffix-cache lookup (no-op for deletion mode)
//   finalize          — post-process results after the A* loop completes
template<class T>
concept GoalHandlerCore = requires(
    T handler,
    const T constHandler,
    const Lines& lines,
    const EditState& state,
    std::vector<std::vector<Result>>& results,
    std::vector<std::vector<Result>>&& movedResults,
    int intVal,
    const std::vector<char>& startActive,
    int& intRef,
    const EditOptimizerParams& params,
    CursorPos goalPos,
    EditSearchStats stats) {
  { constHandler.isGoalReached(lines) } -> std::same_as<bool>;
  { handler.tryUseSuffixCache(state, results, intVal, startActive,
                              intRef, intRef) } -> std::same_as<SuffixCacheResult>;
  { handler.finalize(std::move(movedResults), lines, lines, params,
                     intVal, intVal, goalPos,
                     std::move(stats)) } -> std::same_as<EditResult>;
};

// ChangeGoalEmitter: the change-mode-only contract.
// These on*Goal methods synthesize goal completions after each transition.
// Only ChangeGoalHandler satisfies this; DeletionGoalHandler does not (and
// should not — deletion mode continues searching instead of emitting goals).
template<class T>
concept ChangeGoalEmitter = requires(
    T handler,
    EditState& mutableState,
    const EditState& constState,
    const CharRange& charRange,
    const CharLineRange& charLineRange,
    const LineCharRange& lineCharRange,
    LineRange lineRange,
    const SequenceBinding& cmd) {
  { handler.onDeletionGoal(mutableState, constState, charRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onDeletionGoal(mutableState, constState, charLineRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onDeletionGoal(mutableState, constState, lineCharRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onLinewiseGoal(mutableState, constState, lineRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onCountedLinewiseGoal(mutableState, constState, lineRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onJoinGoal(mutableState, constState, cmd) } -> std::same_as<GoalStates>;
};

template<bool PureDeletion, GoalHandlerCore ModePolicy>
  requires (PureDeletion || ChangeGoalEmitter<ModePolicy>)
struct EditTransitionDispatcher {
  ModePolicy& mode;
  const EditState& baseState;
  EditSearchStats& stats;
  EditPriorityQueue& pq;
  EditCostMap& costMap;
  std::vector<int>& pendingByStart;
  const std::vector<char>& startActive;
  const EffortBank& bank;
  const EditBoundary& editBoundary;
  const Config& config;
  double effortWeight;
  double distanceWeight;
  int leftColOffset;
  int rightColOffset;

  void emitState(EditState&& state) {
    stats.incrementMotionsEmitted();
    Search::enqueueImprovedEditState(
        std::move(state), pq, costMap, pendingByStart, startActive);
  }

  void continueWithEdit(EditState&& next, const SequenceBinding& sourceCmd, double hCost) {
    bool isDot = baseState.hasLastEdit() &&
                 baseState.getLastEditCount() == sourceCmd.count &&
                 baseState.getLastEditBase() == sourceCmd.base.seq.view();
    if (isDot) {
      next.recordSearch(".", bank[KSId::Period], effortWeight, hCost, config);
    } else {
      next.recordSearch(sourceCmd.count, sourceCmd.base.seq.view(),
                        sourceCmd.effort, effortWeight, hCost, config);
      next.setLastEdit(sourceCmd.count, sourceCmd.base.seq.view());
    }
    emitState(std::move(next));
  }

  void moveByMotion(const CursorPos& newPos, const SequenceBinding& motionCmd) {
    assert(motionCmd.count == 0 && "Boundary motions should not carry counts");
    EditState next = baseState;
    next.setPos(newPos);
    next.recordSearch(motionCmd.base.seq.view(), motionCmd.effort,
                      effortWeight,
                      weightedHeuristicCost(next.getLines(), leftColOffset,
                                            rightColOffset, distanceWeight),
                      config);
    emitState(std::move(next));
  }

  template<class RangeT>
  void deleteRange(const RangeT& range, const SequenceBinding& sourceCmd) {
    EditState afterDeletion =
        applyDeletionTransition(baseState, range, sourceCmd.base.seq.view());
    if (!finishTransition(afterDeletion, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterDeletion), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onDeletionGoal(afterDeletion, baseState, range, sourceCmd));
    }
  }

  void deleteLinewise(LineRange range, const SequenceBinding& sourceCmd) {
    EditState afterDeletion =
        baseState.afterMultiLinewiseDeletion(range, editBoundary.hasLinesBelow());
    if (!finishLinewiseTransition(afterDeletion, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterDeletion), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onLinewiseGoal(afterDeletion, baseState, range, sourceCmd));
    }
  }

  void deleteCountedLinewise(LineRange range, const SequenceBinding& sourceCmd) {
    EditState afterDeletion =
        baseState.afterMultiLinewiseDeletion(range, editBoundary.hasLinesBelow());
    if (!finishLinewiseTransition(afterDeletion, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterDeletion), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onCountedLinewiseGoal(afterDeletion, baseState, range, sourceCmd));
    }
  }

  void joinLines(bool addSpace, const SequenceBinding& sourceCmd) {
    EditState afterJoin = baseState.afterJoin(addSpace);
    if (!finishTransition(afterJoin, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterJoin), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onJoinGoal(afterJoin, baseState, sourceCmd));
    }
  }

  void joinCountedLines(bool addSpace, const SequenceBinding& sourceCmd) {
    EditState afterJoin = baseState.afterMultiJoin(sourceCmd.count, addSpace);
    if (!finishTransition(afterJoin, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterJoin), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onJoinGoal(afterJoin, baseState, sourceCmd));
    }
  }

private:
  void emitGoalStates(GoalStates&& goals) {
    emitState(std::move(goals.primary));
    if (goals.dotVariant) emitState(std::move(*goals.dotVariant));
  }

  bool finishTransition(EditState& next, const SequenceBinding& sourceCmd) {
    const Lines& nextLines = next.getLines();
    if (mode.isGoalReached(nextLines)) return true;

    continueWithEdit(
        std::move(next),
        sourceCmd,
        weightedHeuristicCost(nextLines, leftColOffset, rightColOffset, distanceWeight));
    return false;
  }

  bool finishLinewiseTransition(EditState& next, const SequenceBinding& sourceCmd) {
    const Lines& nextLines = next.getLines();
    bool lineOutOfBounds = next.getPos().line < 0 ||
                           next.getPos().line >= static_cast<int>(nextLines.size());
    if (mode.isGoalReached(nextLines)) {
      return !lineOutOfBounds || PureDeletion;
    }
    if (lineOutOfBounds) return false;

    continueWithEdit(
        std::move(next),
        sourceCmd,
        weightedHeuristicCost(nextLines, leftColOffset, rightColOffset, distanceWeight));
    return false;
  }
};
