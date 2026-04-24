#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Boundary/MotionBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Rejected.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

// =============================================================================
// Explore session
// =============================================================================
// The user-facing, step-through state machine on top of a pre-computed
// composition plan. Drives the `:Vimfy explore` scratch buffer.
//
// Responsibilities — the Session owns:
//   - The persistent composition plan. Computed once at construction via
//     `CompositionOptimizer::optimize` and stored in `plan_`. The plan is a
//     sequence of diffs / fenceposts describing how to transform initial →
//     goal; the Session never re-plans, only advances through it. Explore
//     consumes this through CompositionResult's per-edit step view
//     (diff + pre/post fenceposts + editResult), not by re-aligning parallel
//     vectors itself.
//   - The phase machine (Step).
//   - Undo/redo history (vectors of State snapshots).
//   - Mutable live State (lines, cursor, accepted sequence/cost).
//
// Session as frontier forwarder:
//   `recommendations(...)` is a composition of optimizer-level frontier
//   modules, queried live from the current (cursor, fencepost, diff) on
//   every call. The Session does NOT hold a warm optimizer — each call
//   rebuilds the relevant candidate set from scratch. Sources:
//
//     - `rankEditFrontier`  (src/Optimizer/EditOptimizer/EditFrontier.h)
//         Edit atoms valid at the cursor position for the current diff.
//
//     - `backfillEditStartMotions` (file-local; only when cursor is inside
//         the diff range and the edit frontier underfills)
//         Ranks motions toward OTHER viable edit starts inside the same
//         diff, so the user still gets next-step guidance after landing on
//         a non-optimal cell.
//
//     - `rankMotionFrontier` (src/Optimizer/MotionOptimizer/MotionFrontier.h)
//         A depth-1 live A* peek from the cursor — ONE expansion level of
//         the motion optimizer's own search graph, scored the same way the
//         full optimizer would score them, then the top K. Immediate next
//         molecules, not full paths.
//
//     - `rankMotionFrontierToLine`  (join-line hint, multiline diffs only)
//
//   Session-local handlers (`EditHandler`, `MotionHandler`) are only for
//   action parsing/validation and strict buffer acceptance rules.
//
// There is no Invalid phase. "Session is in a bad state" is either:
//   - A recoverable user mistake → the action returns Rejected, state unchanged.
//   - A programming-invariant failure → `assert` and crash.
// Lua tears down sessions via explore_destroy when the scratch buffer goes
// away; there's no need to keep a corpse alive.

namespace Explore {

// Phase enum — one of three states. Paired with Step below for the
// phase-specific payload.
enum class Phase : uint8_t {
  ApproachEdit,   // user is moving cursor toward the current edit's target
  PendingInsert,  // an edit has entered insert mode; remainingText must be typed
  Completed,      // all edits finished; commit or cancel
};

// Tagged struct holding Phase + phase-specific fields. Constructed only via
// factory functions to guarantee well-formed combos. Fields unused by some
// kinds are left defaulted.
struct Step {
  Phase kind = Phase::Completed;
  int editIndex = -1;            // ApproachEdit | PendingInsert
  std::string remainingText;     // PendingInsert

  static Step approachEdit(int editIndex);
  static Step pendingInsert(int editIndex, std::string remainingText);
  static Step completed();

  bool operator==(const Step&) const = default;
};

// One ranked suggestion returned by Session::recommendations.
struct Recommendation {
  std::string text;
  std::string kind;              // "motion" | "edit"
  double cost = 0.0;
  double totalPathCost = 0.0;
  int landingRow = 0;
  int landingCol = 0;
  // Insert-mode text the user must type after `text` to complete the edit
  // (e.g. `"m"` for atom `"s"` when the planned sequence is `"sm<Esc>"`).
  // Empty for motion recs and for normal-mode-only edits like `x` / `rm`.
  std::string typedText;
};

// Mutable state snapshot. The Session owns one live State and a history of
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
// Session::state() — no copy bundled into the result.
struct Applied {
  bool crossedEditBoundary = false;
};

using Outcome = std::expected<Applied, Rejected>;

class Session {
public:
  Session(Lines initialLines, CursorPos initialPos,
          Lines goalLines, CursorPos goalPos,
          MotionBoundary boundary, NavContext navContext, Config config,
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
  //   1. Edit frontier at the cursor (via rankEditFrontier).
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
  // See MotionFrontier.h and EditFrontier.h for the per-module details.
  //
  // Both default to `false` (dedup on). The flag is forwarded to the
  // underlying frontier modules so generation respects it end-to-end and
  // the display layer never throws anything away post-hoc.
  std::vector<Recommendation> recommendations(
      int maxCount,
      bool allowMultipleMotionsPerPosition = false,
      bool allowMultipleEditsPerPosition = false) const;

  // --- Actions ---
  // Each action produces a new state (on Applied) or a Rejected reason
  // (state unchanged). No action can invalidate the session — programming-
  // invariant failures assert instead.
  Outcome applyMotion(std::string_view motionText);
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
  // Immutable session context
  Lines goalLines_;
  CursorPos goalPos_;
  MotionBoundary boundary_;
  NavContext navContext_;
  Config config_;
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

  // Gate helper: succeed iff step is PendingInsert, returning the edit index.
  std::expected<int, Rejected> requirePendingInsert(std::string_view action) const;

  // Advance phase after an edit completes: ApproachEdit(i) → ApproachEdit(i+1)
  // (snapping lines to the next fencepost) or Completed at end of plan.
  Applied afterEditCompleted(State next);
};

const char* to_string(Phase phase);

}  // namespace Explore
