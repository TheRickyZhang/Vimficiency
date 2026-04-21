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
//   - The composition plan (CompositionResult) + derived fenceposts
//     (linesAfterEdit_). Computed once at construction.
//   - The phase machine (Step).
//   - Undo/redo history (vectors of State snapshots).
//   - Mutable live State (lines, cursor, accepted sequence/cost).
//
// Content-level decisions are delegated to stateless handler namespaces in
// sibling headers:
//   - Explore::MotionHandler — what motions exist? does this motion parse?
//   - Explore::EditHandler   — what edit atoms exist here? does this buffer
//                              state match the planned fencepost?
//
// Handlers wrap optimizer artifacts (EditResult, simulateMotions, getEffort)
// and are the single place to keep in sync when optimizer semantics change.
// Session logic is independent of optimizer internals.
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

// Rejections return the reason. State is guaranteed unchanged when the
// session returns a Rejected outcome.
struct Rejected {
  std::string reason;
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

  // Top-K ranked next-step candidates. Empty in PendingInsert / Completed.
  std::vector<Recommendation> recommendations(int maxCount) const;

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

  // Composition plan + fencepost cache (size == totalEdits_ + 1)
  std::optional<CompositionResult> plan_;
  std::vector<Lines> linesAfterEdit_;

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
