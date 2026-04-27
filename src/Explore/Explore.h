#pragma once

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
#include "Optimizer/NavOptimizer/NavFrontier.h"
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
//   1. Phase gate. Use `requireApproachEdit` / `requirePendingInsert`, or
//      `requireApproachEditWithPlan` if the action references `plan_`.
//   2. Reported cursor (when the action takes one from outside): must pass
//      `isCursorOnConcreteBufferCell` against the lines being committed,
//      and the boundary check via `MovementHandler::finishMove` if motion-y.
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

// Three phase alternatives. State::step is the variant Step below; each
// alternative carries exactly the fields that phase needs — no shared
// fields with empty-meaning sentinels.
struct Approach      { int editIndex = 0; bool operator==(const Approach&) const = default; };
struct PendingInsert { int editIndex = 0; std::string remainingText;
                       bool operator==(const PendingInsert&) const = default; };
struct Completed     { bool operator==(const Completed&) const = default; };

using Step = std::variant<Approach, PendingInsert, Completed>;

// One ranked suggestion returned by View::recommendations. The variant
// alternative IS the kind (Movement vs Edit), so there's no string tag —
// each alternative carries exactly the fields its kind needs, sharing
// `molecule` / `goalPos` / `cost` via FrontierItem.
using Suggestion = std::variant<NavFrontierItem, TransformFrontierItem>;

// --- Helpers ---
// Overload-set wire labels for each Step alternative. Adding a fourth
// alternative = add one overload; no chain to thread through.
inline std::string_view stepKindName(const Approach&)      { return "ApproachEdit"; }
inline std::string_view stepKindName(const PendingInsert&) { return "PendingInsert"; }
inline std::string_view stepKindName(const Completed&)     { return "Completed"; }
inline std::string_view stepKindName(const Step& s) {
  return std::visit([](const auto& a) { return stepKindName(a); }, s);
}

inline std::string_view suggestionKind(const NavFrontierItem&)       { return "movement"; }
inline std::string_view suggestionKind(const TransformFrontierItem&) { return "edit"; }
inline std::string_view suggestionKind(const Suggestion& s) {
  return std::visit([](const auto& a) { return suggestionKind(a); }, s);
}

// Common base accessor — the FrontierItem shared subset (molecule, goalPos,
// cost). Use when callers need only the shared fields and don't care which
// alternative holds them.
inline const FrontierItem& base(const Suggestion& s) {
  return std::visit(
      [](const FrontierItem& f) -> const FrontierItem& { return f; }, s);
}

// Mutable state snapshot. The View owns one live State and a history of
// prior snapshots in its undo/redo stacks.
struct State {
  Step step;
  int acceptedRevision = 0;
  Lines lines;
  CursorPos cursor{0, 0};
  std::string acceptedSeq;
  double acceptedCost = 0.0;

  bool operator==(const State&) const = default;
};

// Action outcomes.
//
// Successful transitions return Applied; Applied carries just a flag about
// whether an edit boundary was crossed (useful for Lua to decide whether to
// rewrite the scratch buffer). The new live state itself is read back via
// View::state() — no copy bundled into the result.
struct Applied {
  bool crossedEditBoundary = false;
};

using Outcome = std::expected<Applied, Rejected>;

class View {
public:
  View(Lines initialLines, CursorPos initialPos,
       Lines goalLines, CursorPos goalPos,
       NavBoundary boundary, NavContext navContext, Config config,
       std::string_view userSequence = "");

  // --- Query ---
  const Step& step() const { return state_.step; }
  const State& state() const { return state_; }
  int totalEdits() const { return totalEdits_; }
  int acceptedRevision() const { return state_.acceptedRevision; }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }

  const Lines& goalLines() const { return goalLines_; }
  CursorPos goalPos() const { return goalPos_; }

  // Half-open [begin, end) range of the current edit's diff in intermediate-
  // buffer coordinates, when in ApproachEdit and a plan exists. Empty otherwise.
  std::optional<std::pair<CursorPos, CursorPos>> currentTargetRange() const;

  // Top-K ranked IMMEDIATE NEXT MOLECULES for the current phase. Empty in
  // PendingInsert / Completed.
  //
  // Each recommendation is one atomic action the user could take right now
  // (`w`, `f;`, `$`, `s`, `cl`, ...), NOT a full sequence to the target.
  // The user picks one, the session advances by exactly that molecule, and
  // the next call to `recommendations(...)` computes a fresh frontier from
  // the new cursor state.
  //
  // Composition (in rank order of how items are appended before the trim):
  //   1. Edit frontier at the cursor (via rankTransformFrontier).
  //   2. Motion backfill toward sibling edit starts (only when cursor is
  //      already inside the diff and the edit frontier underfills).
  //   3. Motion frontier toward the diff target (only when cursor is
  //      outside the diff) — depth-1 live A* peek.
  //   4. Optional join-line motion hint for multiline diffs.
  // Final step trims to `maxCount`.
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
  Outcome beginEdit(bool entersInsertMode, std::string_view requiredTypedText = "");
  Outcome consumeInsertText(std::string_view typedChunk);
  Outcome exitInsertMode();
  // Buffer-state completion for PendingInsert. Accept iff `newLines` matches
  // the planned fencepost after the in-flight edit; on accept advances past
  // PendingInsert regardless of any residual `remainingText` (the buffer is
  // the source of truth). On mismatch, state is unchanged.
  Outcome acceptInsertExit(const Lines& newLines, CursorPos newCursor,
                           std::string_view rawKeys);
  // Abort a PendingInsert without polluting redo. Pops the matching undo
  // entry (the beginEdit commit) in-place — intended for rejected or
  // abandoned insert-mode entries.
  Outcome cancelPendingInsert();
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
  // initial plan computation and any per-step recomputation
  // (e.g. join plans during recommendations()), so the two paths cannot drift.
  CompositionOptimizerParams compositionParams_;
  int totalEdits_ = 0;

  // Persistent composition plan + step-scoped frontier artifacts.
  std::optional<CompositionResult> plan_;

  // Live state + undo/redo history
  State state_;
  std::vector<State> undo_;
  std::vector<State> redo_;

  // --- Internal helpers ---

  // Push current state onto undo, clear redo, move `next` into live state.
  // Returns Applied{crossedEditBoundary}.
  Applied commit(State next, bool crossedEditBoundary = false);

  // Gate helper: succeed iff step is ApproachEdit, returning the edit index.
  // The `action` label is woven into the rejection reason for diagnostics.
  std::expected<int, Rejected> requireApproachEdit(std::string_view action) const;

  // ApproachEdit gate + plan presence in one call. Use for actions that
  // dereference `plan_->stepAt(...)` (applyEdit, acceptBufferState,
  // beginEdit). Pure-motion approaches (no plan) are rejected here.
  std::expected<int, Rejected> requireApproachEditWithPlan(
      std::string_view action) const;

  // Gate helper: succeed iff step is PendingInsert, returning the edit index.
  std::expected<int, Rejected> requirePendingInsert(std::string_view action) const;

  // True when Explore is guiding only cursor motion because the buffer
  // already matches the goal text and there is no edit plan to execute.
  bool isPureMotionApproach() const;

  // Advance phase after an edit completes: ApproachEdit(i) → ApproachEdit(i+1)
  // (snapping lines to the next fencepost) or Completed at end of plan.
  // `editIndex` is the index of the edit that just completed.
  Applied afterEditCompleted(State next, int editIndex);
};

}  // namespace Explore
