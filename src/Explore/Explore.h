#pragma once

#include <cassert>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/FrontierCommon.h"
#include "Optimizer/TransformOptimizer/TransformFrontier.h"
#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

// =============================================================================
// Explore view
// =============================================================================
// Middle layer between Lua (InteractiveExports.cpp) and the optimizer lib (`src/Optimizer/`).
// Lua calls in via the FFI exports in `LuaExports/InteractiveExports.cpp`.
//
// TLDR: review the lifecycle
//
// Conventions:
//   - Plan is computed once at construction and never re-planned. Each
//     `recommendations(...)` call rebuilds frontiers live from
//     `Optimizer/*Frontier.h` modules — no warm optimizer is cached here.
//   - User mistakes return `Rejected` with state unchanged; programming
//     invariants `assert`. There is no Invalid phase.
//   - Lifecycle is Lua's job: `explore_destroy` tears down on scratch close.
//   - Action parsing/validation lives in `EditHandler`/`MovementHandler`,
//     not inline in View methods.
//
// Action contract — every accept*/apply* satisfies these or returns Rejected
// with state unchanged. New actions: walk this list explicitly.
//   1. Phase gate. Use the require* helpers for the relevant phase/action.
//   2. Reported cursor (when the action takes one from outside): must pass
//      `Lines::contains` against the lines being committed, and the boundary
//      check via `MovementHandler::finishMove` if motion-y.
//   3. Reported lines (when the action takes them from outside): must match
//      the expected fencepost (current and/or next) — go through
//      `EditHandler::validateBufferState`.
//   4. Non-empty raw keys: must parse via `parseSequence` (edit-bearing) or
//      `parseMotions` (motion-only) — use `appendRawKeysOrReject`. Silently
//      dropping unparseable keys undercounts cost and hides bugs.
//   5. State unchanged on any Reject — never partially mutate `state_`. The
//      "build a `next` snapshot, commit at the end" pattern enforces this.
//   6. Successful return must `commit(...)` (or call a helper that does) so
//      `acceptedRevision` increments and undo history is updated.

namespace Explore {

// Phase model: a session has E edits and E+1 navigation segments
// alternating between them. Phase identifies what the user is currently
// doing in that grid:
//
//   Navigate(i)  for i ∈ [0, totalEdits]   target = plan.navTarget(i)
//                                           (= edit i's diff range for i<E,
//                                            = goalPos for i==E)
//   Transform(i) for i ∈ [0, totalEdits)   apply edit i
//   Insert(i)    for i ∈ [0, totalEdits)   insert-mode continuation for edit i
//
// "Completion" is a derived predicate (see View::isCompleted), not a phase
// alternative. Pure-motion sessions are the degenerate E == 0 case; the only
// reachable phase is Navigate(0) with target = goalPos.
struct Navigate  { int index = 0; bool operator==(const Navigate&) const = default; };
struct Transform { int index = 0; bool operator==(const Transform&) const = default; };
struct Insert    { int index = 0; bool operator==(const Insert&) const = default; };
using Phase = std::variant<Navigate, Transform, Insert>;

template <class... Ts>
struct PhaseVisitor : Ts... { using Ts::operator()...; };
template <class... Ts>
PhaseVisitor(Ts...) -> PhaseVisitor<Ts...>;

inline std::string_view phaseKindName(const Phase& p) {
  return std::visit(PhaseVisitor{
      [](const Navigate&)  { return std::string_view{"Navigate"}; },
      [](const Transform&) { return std::string_view{"Transform"}; },
      [](const Insert&)    { return std::string_view{"Insert"}; },
  }, p);
}

// Phase index always carries a value (no optional) — the variant alternative
// encodes which subrange the index lives in.
inline int phaseIndex(const Phase& p) {
  return std::visit([](auto&& x) { return x.index; }, p);
}

// One ranked suggestion returned by View::recommendations. The variant
// alternative IS the kind (Nav vs Transform); the optimizer-produced
// types ARE the wire types — Explore doesn't introduce a parallel
// Suggestion hierarchy, since the optimizer outputs already carry the
// minimum data the wire needs.
using Suggestion = std::variant<FrontierItem, TransformFrontierItem>;

inline std::string_view suggestionKind(const FrontierItem&)          { return "movement"; }
inline std::string_view suggestionKind(const TransformFrontierItem&) { return "edit"; }
inline std::string_view suggestionKind(const Suggestion& s) {
  return std::visit([](const auto& a) { return suggestionKind(a); }, s);
}

// Common base accessor — the FrontierItem shared subset (token, goalPos,
// cost). Use when callers need only the shared fields and don't care which
// alternative holds them.
inline const FrontierItem& base(const Suggestion& s) {
  return std::visit(
      [](const FrontierItem& f) -> const FrontierItem& { return f; }, s);
}

// Mutable state snapshot. The View owns one live State and a history of
// prior snapshots in its undo/redo stacks.
struct State {
  Phase phase = Navigate{0};
  int acceptedRevision = 0;
  Lines lines;
  CursorPos cursor{0, 0};
  std::string acceptedSeq;
  double acceptedCost = 0.0;

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
  int acceptedRevision() const { return state_.acceptedRevision; }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }

  // The session is complete iff the cursor has reached `goalPos` after all
  // edits — i.e. phase is `Navigate{totalEdits()}` and the cursor matches
  // the user's goalPos. Not sticky: moving the cursor away after reaching
  // goal flips this back to false.
  bool isCompleted() const;

  const Lines& goalLines() const { return goalLines_; }
  CursorPos goalPos() const { return goalPos_; }

  // Half-open [begin, end) target for the current phase: edit diff range for
  // Navigate/Transform(i), final goal point for Navigate(totalEdits), empty
  // during Insert.
  std::optional<std::pair<CursorPos, CursorPos>> currentTargetRange() const;

  // Top-K ranked IMMEDIATE NEXT MOLECULES for the current phase. Empty in
  // Insert; a completed Navigate(totalEdits) naturally has no useful motion
  // target left.
  //
  // Each recommendation is one atomic action the user could take right now
  // (`w`, `f;`, `$`, `s`, `cl`, ...), NOT a full sequence to the target.
  // The user picks one, the session advances by exactly that token, and
  // the next call to `recommendations(...)` computes a fresh frontier from
  // the new cursor state.
  //
  // Composition by phase:
  //   - Navigate(i): motion frontier toward navTarget(i) — depth-1 live A*
  //     peek (rankNavFrontier).
  //   - Transform(i): edit frontier at the cursor (rankTransformFrontier).
  //     Motion suggestions never appear in Transform — the phase invariant
  //     keeps the cursor inside the diff, so the immediate next token is
  //     always an edit. The user may still take a motion via
  //     `applyMovement`; the Lua layer is responsible for that affordance.
  //   - Insert(i): empty.
  // Final trim caps the list at `maxCount`.
  //
  // Two independent dedup knobs — motions and edits use DIFFERENT dedup
  // keys because their pedagogical axes differ:
  //   - motions dedup by LANDING CELL: `w`/`W`/`e` all landing on the
  //     same cell is redundant (the cell is the outcome).
  //   - edits dedup by SEQUENCE TEXT: `rm`, `sm<Esc>`, `cl m<Esc>` all
  //     landing at the same post-edit cursor are DISTINCT commands and
  //     all surface; the command shape is the outcome.
  // See NavFrontier.h and TransformFrontier.h for the per-module details.
  //
  // Both default to `false` (dedup on). The flag is forwarded to the
  // underlying frontier modules so generation respects it end-to-end and
  // the display layer never throws anything away post-hoc.
  std::vector<Suggestion> recommendations(
      int maxCount,
      bool allowMultipleMovementsPerPosition = false,
      bool allowMultipleEditsPerPosition = false) const;

  // --- Actions ---
  // Each action produces a new state (on Applied) or a Rejected reason
  // (state unchanged). No action can invalidate the view — programming-
  // invariant failures assert instead.
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

  // Live state + undo/redo history
  State state_;
  std::vector<State> undo_;
  std::vector<State> redo_;

  // --- Internal helpers ---

  // Push current state onto undo, clear redo, move `next` into live state.
  // Always returns success.
  Outcome commit(State next);

  Phase phaseForEditCursor(int editIndex, CursorPos cursor) const;
  bool movementStaysInTransformRange(CursorPos cursor) const;
  std::vector<Suggestion> recommendNavigate(
      int navIndex,
      int maxCount,
      bool allowMultipleMovementsPerPosition) const;
  std::vector<Suggestion> recommendTransform(
      int editIndex,
      int maxCount,
      bool allowMultipleEditsPerPosition) const;

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
