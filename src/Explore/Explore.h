#pragma once

#include <cassert>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/EditSequenceSpan.h"
#include "Optimizer/FrontierCommon.h"
#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

#include <array>
#include <cstdint>

/* Scratch-buffer Explore session. Phase indices refer to composition planned
   edits; Navigate(totalEdits) is the final cursor segment. */

class OptimizerParamOverrides;
struct FrontierQuery;

namespace Explore {

struct Navigate  {
  int index = 0;
  bool operator==(const Navigate&) const = default;
};
struct Transform {
  int index = 0;
  bool operator==(const Transform&) const = default;
};
struct Insert    {
  int index = 0;
  bool operator==(const Insert&) const = default;
};

using Phase = std::variant<Navigate, Transform, Insert>;

template <class... Ts>
struct overload : Ts... { using Ts::operator()...; };

inline std::string_view phaseName(const Phase& p) {
  return std::visit(overload{
      [](const Navigate&)  { return std::string_view{"Navigate"}; },
      [](const Transform&) { return std::string_view{"Transform"}; },
      [](const Insert&)    { return std::string_view{"Insert"}; },
  }, p);
}

inline int phaseIndex(const Phase& p) {
  return std::visit([](auto&& x) { return x.index; }, p);
}

// Per-State edit span list, sized to the composition optimizer's hard cap of
// 16 edits. Fixed storage keeps undo/redo snapshots allocation-free at the
// cost of ~128 bytes per snapshot. Equality intentionally compares only the
// active prefix `[0, size)` so trailing zero-initialized entries don't break
// State::operator==.
struct EditSpanList {
  std::array<EditSequenceSpan, 16> spans{};
  uint8_t size = 0;

  bool operator==(const EditSpanList& other) const {
    if (size != other.size) return false;
    for (uint8_t i = 0; i < size; ++i) {
      if (!(spans[i] == other.spans[i])) return false;
    }
    return true;
  }

  void push(EditSequenceSpan s) {
    assert(size < spans.size() && "EditSpanList overflow (>16 edits)");
    spans[size++] = s;
  }
};

struct State {
  Phase phase = Navigate{0};
  Lines lines;
  CursorPos cursor{0, 0};
  std::string seq;
  double cost = 0.0;
  // Replacement users may delete first in Normal mode, then enter Insert.
  // Keep the eventual edit span anchored at the deletion's first byte.
  bool hasPartialEditSpan = false;
  uint32_t partialEditSpanBegin = 0;

  // Plan-aligned byte spans for completed edits; undo/redo pops in lockstep.
  EditSpanList editSpans;

  bool operator==(const State&) const = default;
};

// Action outcomes. Successful transitions return void; the new live state
// is read back via View::state(). Failures return Rejected with a reason.
using Outcome = std::expected<void, Rejected>;

class View {
public:
  View(Lines initialLines, CursorPos initialPos,
       Lines goalLines, CursorPos goalPos,
       NavBoundary boundary, NavContext navContext, Config config,
       std::string_view userSequence = "");

  // --- Query ---
  const Phase& phase() const { return state_.phase; }
  const State& state() const { return state_; }
  int totalEdits() const { return totalEdits_; }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }

  bool isCompleted() const;

  const Lines& goalLines() const { return goalLines_; }
  CursorPos goalPos() const { return goalPos_; }

  // Half-open [begin, end) target for the current phase: edit diff range for
  // Navigate/Transform/Insert(i), final goal point for Navigate(totalEdits).
  // Insert(i) reports the same diff range as Navigate/Transform(i); whether
  // to render it is a UI policy decision (see tags.lua).
  std::pair<CursorPos, CursorPos> currentTargetRange() const;

  // `overrides` carries the explore-session's optimizer tweaks (resolved
  // from sidecar + user setup); null = use C++ defaults. Same blob shape
  // as `vf_analyze`.
  std::vector<Suggestion> recommendations(
      int maxCount,
      const OptimizerParamOverrides* overrides = nullptr,
      SuggestionSortMode sortMode = SuggestionSortMode::Effort) const;

  // Explored/optimal rows are plan-aligned; user input has no such boundary.
  struct HeaderRows {
    std::vector<std::string> explored;
    std::vector<std::vector<std::string>> optimal;
  };
  HeaderRows headerRows() const;

  Outcome applyMovement(std::string_view movementText);
  Outcome acceptCursorMove(CursorPos newCursor, std::string_view rawKeys);
  Outcome applyEdit(std::string_view text);
  Outcome acceptBufferState(const Lines& newLines, CursorPos newCursor,
                            std::string_view rawKeys);
  Outcome beginInsert();
  // Buffer-state completion for Insert. Accept iff `newLines` matches the
  // planned fencepost after the in-flight edit. On mismatch, state is unchanged.
  Outcome acceptInsertExit(const Lines& newLines, CursorPos newCursor,
                           std::string_view rawKeys);
  Outcome acceptSnapshot(const Lines& liveLines, CursorPos liveCursor,
                         std::string_view rawKeys, bool insertMode);
  // Abort Insert without polluting redo. Pops the matching undo entry
  // (the beginInsert commit) in-place.
  Outcome cancelInsert();
  Outcome undo();
  Outcome redo();

private:
  // Immutable view context
  Lines goalLines_;
  CursorPos goalPos_;
  NavBoundary boundary_;
  NavContext navContext_;
  Config config_;
  // Single source of truth for composition params: shared between the
  // initial plan computation and any per-phase recomputation
  // (e.g. join plans during recommendations()), so the two paths cannot drift.
  CompositionOptimizerParams compositionParams_;
  int totalEdits_ = 0;

  // Persistent composition plan + phase-scoped frontier artifacts. Always
  // populated — pure-motion sessions use a 0-edit plan whose only nav target
  // is `goalPos`.
  CompositionResult plan_;

  // Per-optimal-result edit byte spans, computed once at construction via
  // CompositionOptimizer::optimizeWithEditSpans. Indexed in lockstep with
  // `plan_.getResults()`. Used by `headerRows()` to render the Optimal-N
  // columns segmented by composition plan rather than by Vim-token kind.
  std::vector<std::vector<EditSequenceSpan>> optimalEditSpans_;

  // Live state + undo/redo history
  State state_;
  std::vector<State> undo_;
  std::vector<State> redo_;

  // --- Internal helpers ---

  // Push current state onto undo, clear redo, move `next` into live state.
  // Always returns success.
  Outcome commit(State next);

  bool hasExecutableEditStartAt(
      int editIndex,
      const Lines& lines,
      CursorPos cursor) const;
  std::vector<CursorPos> executableEditStartPositions(
      int editIndex,
      const Lines& lines) const;
  Phase phaseForCursor(
      int editIndex,
      const Lines& lines,
      CursorPos cursor) const;

  FrontierQuery makeFrontierQuery(
      int maxCount,
      const OptimizerParamOverrides* overrides,
      SuggestionSortMode sortMode) const;

  std::vector<Suggestion> recommendNavigate(
      int navIndex,
      int maxCount,
      const OptimizerParamOverrides* overrides,
      SuggestionSortMode sortMode) const;
  std::vector<Suggestion> recommendTransform(
      int editIndex,
      int maxCount,
      const OptimizerParamOverrides* overrides,
      SuggestionSortMode sortMode) const;
  // Insert emits the canonical typed text (no trailing <Esc>).
  std::vector<Suggestion> recommendInsert(
      int editIndex,
      int maxCount) const;

  // Motion gate: succeed iff phase is Navigate/Transform, returning the
  // navigation/edit index. Navigate(totalEdits) is valid here because the
  // post-final-edit nav segment still accepts cursor movement.
  std::expected<int, Rejected> requireNavigateOrTransform(
      std::string_view action) const;

  std::expected<int, Rejected> requireTransform(std::string_view action) const;

  // Edit-bearing gate: succeed iff phase is Navigate/Transform and the index
  // references a real planned edit. Use this before plannedEditAt(...).
  std::expected<int, Rejected> requirePlannedEditTarget(
      std::string_view action) const;

  // Gate helper: succeed iff phase is Insert, returning the edit index.
  std::expected<int, Rejected> requireInsert(std::string_view action) const;

  // Advance phase after an edit completes: Transform/Insert(i) →
  // Navigate/Transform(i+1) (snapping lines to the next fencepost). When
  // `editIndex == totalEdits_ - 1`, transitions to Navigate(totalEdits_) —
  // the post-final-edit nav segment. Completion is then a derived predicate
  // (see isCompleted) that triggers when the cursor reaches goalPos.
  Outcome afterEditCompleted(State next, int editIndex);
};

}  // namespace Explore
