#pragma once

#include <concepts>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "TransformState.h"
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
#include "Boundary/TransformBoundary.h"
#include "VimCore/VimEditUtils.h"

struct TransformStateComparator {
  bool operator()(const TransformState& a, const TransformState& b) const {
    double aPriority = a.getCost();
    double bPriority = b.getCost();
    if (aPriority != bPriority) return aPriority > bPriority;
    return a.getStartIndex() > b.getStartIndex();
  }
};

using TransformPriorityQueue = std::priority_queue<TransformState, std::vector<TransformState>, TransformStateComparator>;
using TransformCostMap = std::unordered_map<TransformStateKey, double, TransformStateKeyHash>;

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

// Forward declarations for concept signatures — avoids pulling in TransformOptimizer.h.
struct TransformOptimizerParams;
struct TransformResult;

// GoalHandlerCore: the shared contract between DeletionGoalHandler and
// ChangeGoalHandler.  Both are consumed by optimizeImpl and
// TransformTransitionDispatcher.
//
//   isGoalReached     — test whether the buffer has reached the goal state
//   tryUseSuffixCache — attempt suffix-cache lookup (no-op for deletion mode)
//   finalize          — post-process results after the A* loop completes
template<class T>
concept GoalHandlerCore = requires(
    T handler,
    const T constHandler,
    const Lines& lines,
    const TransformState& state,
    std::vector<std::vector<Result>>& results,
    std::vector<std::vector<Result>>&& movedResults,
    int intVal,
    const std::vector<char>& startActive,
    int& intRef,
    const TransformOptimizerParams& params,
    CursorPos goalPos,
    TransformSearchStats stats,
    std::vector<std::vector<CursorPos>>&& cursorGoals) {
  { constHandler.isGoalReached(lines) } -> std::same_as<bool>;
  { handler.tryUseSuffixCache(state, results, intVal, startActive,
                              intRef, intRef) } -> std::same_as<SuffixCacheResult>;
  { handler.finalize(std::move(movedResults), lines, lines, params,
                     intVal, intVal, goalPos,
                     std::move(stats),
                     std::move(cursorGoals)) } -> std::same_as<TransformResult>;
};

// ChangeGoalEmitter: the change-mode-only contract.
// These on*Goal methods synthesize goal completions after each transition.
// Only ChangeGoalHandler satisfies this; DeletionGoalHandler does not (and
// should not — deletion mode continues searching instead of emitting goals).
template<class T>
concept ChangeGoalEmitter = requires(
    T handler,
    const TransformEditorState& editorState,
    const TransformState& constState,
    const CharRange& charRange,
    const CharLineRange& charLineRange,
    const LineCharRange& lineCharRange,
    LineRange lineRange,
    const SequenceBinding& cmd) {
  { handler.onDeletionGoal(editorState, constState, charRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onDeletionGoal(editorState, constState, charLineRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onDeletionGoal(editorState, constState, lineCharRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onLinewiseGoal(editorState, constState, lineRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onCountedLinewiseGoal(editorState, constState, lineRange, cmd) } -> std::same_as<GoalStates>;
  { handler.onJoinGoal(editorState, constState, cmd) } -> std::same_as<GoalStates>;
};

template<bool PureDeletion, GoalHandlerCore ModePolicy>
  requires (PureDeletion || ChangeGoalEmitter<ModePolicy>)
struct TransformTransitionDispatcher {
  ModePolicy& mode;
  const TransformState& baseState;
  TransformSearchStats& stats;
  TransformPriorityQueue& pq;
  TransformCostMap& costMap;
  std::vector<int>& pendingByStart;
  const std::vector<char>& startActive;
  const EffortBank& bank;
  const TransformBoundary& transformBoundary;
  const Config& config;
  double effortWeight;
  double distanceWeight;
  int leftColOffset;
  int rightColOffset;
  bool allowLinewisePureDeletion;

  void emitState(TransformState&& state) {
    stats.incrementMotionsEmitted();
    Search::enqueueImprovedTransformState(
        std::move(state), pq, costMap, pendingByStart, startActive);
  }

  TransformStateFactory stateFactory() const {
    return TransformStateFactory(config, effortWeight);
  }

  void continueWithEdit(TransformEditorState&& next, const SequenceBinding& sourceCmd, double hCost) {
    const DotRepeat& last = baseState.getLastEdit();
    bool isDot = !last.empty() &&
                 last.count == sourceCmd.count &&
                 last.base == sourceCmd.base.seq.view();
    if (isDot) {
      emitState(stateFactory().afterCommand(
          baseState, std::move(next), ".", bank[KSId::Period], hCost));
    } else {
      emitState(stateFactory().afterEditCommand(
          baseState, std::move(next), sourceCmd.count, sourceCmd.base.seq.view(),
          sourceCmd.effort, hCost));
    }
  }

  void moveByMotion(const CursorPos& newPos, const SequenceBinding& motionCmd) {
    assert(motionCmd.count == 0 && "Boundary motions should not carry counts");
    TransformEditorState next = TransformSimulator::withPos(
        baseState.getEditorState(), newPos);
    emitState(stateFactory().afterCommand(
        baseState, std::move(next), motionCmd.base.seq.view(), motionCmd.effort,
        weightedHeuristicCost(baseState.getLines(), leftColOffset,
                              rightColOffset, distanceWeight)));
  }

  void emitChangeGoalForResolved(const TransformEditorState& afterDeletion,
                                 const VimCore::ResolvedDeleteRange& resolved,
                                 const SequenceBinding& sourceCmd) {
    if constexpr (!PureDeletion) {
      switch (resolved.kind) {
        case VimCore::ResolvedDeleteRangeKind::Characterwise:
          emitGoalStates(mode.onDeletionGoal(
              afterDeletion, baseState, resolved.charRange, sourceCmd));
          return;
        case VimCore::ResolvedDeleteRangeKind::CharLine:
          emitGoalStates(mode.onDeletionGoal(
              afterDeletion, baseState, resolved.charLineRange, sourceCmd));
          return;
        case VimCore::ResolvedDeleteRangeKind::LineChar:
          emitGoalStates(mode.onDeletionGoal(
              afterDeletion, baseState, resolved.lineCharRange, sourceCmd));
          return;
        case VimCore::ResolvedDeleteRangeKind::Linewise:
          emitGoalStates(mode.onLinewiseGoal(
              afterDeletion, baseState, resolved.lineRange, sourceCmd));
          return;
      }
    }
  }

  void deleteResolved(const ResolvedEditAction& action,
                      const SequenceBinding& sourceCmd) {
    const VimCore::ResolvedDeleteRange& deleteEffect = action.deleteEffect;
    if (!action.deleteEffectValid) {
      if constexpr (PureDeletion) return;
    }
    if constexpr (PureDeletion) {
      if (deleteEffect.kind == VimCore::ResolvedDeleteRangeKind::Linewise &&
          !allowLinewisePureDeletion) {
        return;
      }
    }
    std::optional<TransformEditorState> afterDeletion;
    bool deleteReachedGoal = false;
    if (action.deleteEffectValid) {
      afterDeletion = TransformSimulator::afterResolvedDeletion(
          baseState.getEditorState(), deleteEffect, lineDeleteContext());
      deleteReachedGoal = finishTransition(*afterDeletion, sourceCmd);
    }
    if constexpr (PureDeletion) {
      if (!deleteReachedGoal) return;
      continueWithEdit(std::move(*afterDeletion), sourceCmd, 0.0);
    } else {
      const VimCore::ResolvedDeleteRange& changeEffect =
          action.effectForChange();
      if (action.changeEffectValid) {
        TransformEditorState afterChangeDeletion =
            TransformSimulator::afterResolvedChangeDeletion(
                baseState.getEditorState(), changeEffect, lineDeleteContext());
        if (canEmitGoalFrom(afterChangeDeletion)) {
          emitChangeGoalForResolved(afterChangeDeletion, changeEffect, sourceCmd);
          return;
        }
      }
      if (action.changeEffect) return;
      if (!deleteReachedGoal) return;
      emitChangeGoalForResolved(*afterDeletion, deleteEffect, sourceCmd);
    }
  }

  void deleteCountedLinewise(LineRange range, const SequenceBinding& sourceCmd) {
    if constexpr (PureDeletion) {
      if (!allowLinewisePureDeletion) return;
    }
    TransformEditorState afterDeletion =
        TransformSimulator::afterMultiLinewiseDeletion(
            baseState.getEditorState(), range, lineDeleteContext());
    if (!finishTransition(afterDeletion, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterDeletion), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onCountedLinewiseGoal(afterDeletion, baseState, range, sourceCmd));
    }
  }

  void joinLines(bool addSpace, const SequenceBinding& sourceCmd) {
    TransformEditorState afterJoin =
        TransformSimulator::afterJoin(baseState.getEditorState(), addSpace);
    if (!finishTransition(afterJoin, sourceCmd)) return;
    if constexpr (PureDeletion) {
      continueWithEdit(std::move(afterJoin), sourceCmd, 0.0);
    } else {
      emitGoalStates(mode.onJoinGoal(afterJoin, baseState, sourceCmd));
    }
  }

  void joinCountedLines(bool addSpace, const SequenceBinding& sourceCmd) {
    TransformEditorState afterJoin =
        TransformSimulator::afterMultiJoin(
            baseState.getEditorState(), sourceCmd.count, addSpace);
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

  bool finishTransition(TransformEditorState& next, const SequenceBinding& sourceCmd) {
    const Lines& nextLines = next.getLines();
    bool lineOutOfBounds = next.getPos().line < 0 ||
                           next.getPos().line >= static_cast<int>(nextLines.size());
    if (!protectedTextPreserved(next)) return false;
    if (stateReachedGoal(next)) return !lineOutOfBounds;
    if (lineOutOfBounds) return false;
    if (cursorOutsideEditableRegion(next)) return false;

    continueWithEdit(
        std::move(next),
        sourceCmd,
        weightedHeuristicCost(nextLines, leftColOffset, rightColOffset, distanceWeight));
    return false;
  }

  bool canEmitGoalFrom(const TransformEditorState& state) const {
    const Lines& lines = state.getLines();
    bool lineOutOfBounds = state.getPos().line < 0 ||
                           state.getPos().line >= static_cast<int>(lines.size());
    return protectedTextPreserved(state) &&
           stateReachedGoal(state) &&
           !lineOutOfBounds;
  }

  bool stateReachedGoal(const TransformEditorState& state) const {
    if constexpr (PureDeletion) {
      return mode.isGoalReached(state.getLines());
    } else {
      return mode.isGoalReached(state);
    }
  }

  VimCore::LineDeleteContext lineDeleteContext() const {
    return {
        .hasLinesAbove = transformBoundary.hasLinesAbove(),
        .hasLinesBelow = transformBoundary.hasLinesBelow(),
    };
  }

  bool cursorOutsideEditableRegion(const TransformEditorState& state) const {
    CursorPos pos = state.getPos();
    const Lines& lines = state.getLines();
    if (pos.line == 0 && pos.col < leftColOffset) return true;
    if (pos.line == lines.lastLine() && rightColOffset > 0 &&
        pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset) {
      return true;
    }
    return false;
  }

  bool protectedTextPreserved(const TransformEditorState& state) const {
    const Lines& lines = state.getLines();
    if (lines.empty()) return false;
    const std::string& prefix = transformBoundary.prefix();
    if (!prefix.empty() && lines.front().rfind(prefix, 0) != 0) {
      return false;
    }
    const std::string& suffix = transformBoundary.suffix();
    if (!suffix.empty()) {
      const std::string& last = lines.back();
      if (last.size() < suffix.size()) return false;
      if (last.compare(last.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
      }
    }
    return true;
  }
};
