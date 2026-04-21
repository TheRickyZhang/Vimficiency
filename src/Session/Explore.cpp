#include "Explore.h"

#include <cassert>
#include <utility>

#include "EditHandler.h"
#include "MotionHandler.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/SequenceParser.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"

using namespace std;

namespace Explore {

// =============================================================================
// Step factories
// =============================================================================

Step Step::approachEdit(int editIndex) {
  return {Phase::ApproachEdit, editIndex, ""};
}

Step Step::pendingInsert(int editIndex, string remainingText) {
  return {Phase::PendingInsert, editIndex, std::move(remainingText)};
}

Step Step::completed() {
  return {Phase::Completed, -1, ""};
}

// =============================================================================
// Session construction
// =============================================================================

Session::Session(
    Lines initialLines,
    CursorPos initialPos,
    Lines goalLines,
    CursorPos goalPos,
    MotionBoundary boundary,
    NavContext navContext,
    Config config,
    string_view userSequence)
    : goalLines_(std::move(goalLines)),
      goalPos_(goalPos),
      boundary_(std::move(boundary)),
      navContext_(navContext),
      config_(std::move(config)) {

  state_.step = Step::completed();
  state_.lines = initialLines;
  state_.cursor = initialPos;

  if (initialLines == goalLines_) {
    // Pure motion goals aren't the explore use case; treat as already done.
    totalEdits_ = 0;
    linesAfterEdit_.push_back(std::move(initialLines));
    return;
  }

  // MIRROR: composition optimizer computes the plan we step through. This
  // call's inputs and semantics must track CompositionOptimizer::optimize.
  CompositionOptimizer opt(config_);
  plan_.emplace(opt.optimize(
      state_.lines, initialPos, goalLines_, goalPos_,
      CompositionOptimizerParams{},
      userSequence,
      boundary_,
      navContext_));

  totalEdits_ = static_cast<int>(plan_->getDiffs().size());

  // MIRROR: composition stores diffs in intermediate-buffer coordinates, so
  // sequential Myers::applyDiffState walks the fencepost chain correctly.
  linesAfterEdit_.reserve(static_cast<size_t>(totalEdits_) + 1);
  linesAfterEdit_.push_back(state_.lines);
  for (const DiffState& diff : plan_->getDiffs()) {
    linesAfterEdit_.push_back(Myers::applyDiffState(diff, linesAfterEdit_.back()));
  }

  if (totalEdits_ > 0) {
    state_.step = Step::approachEdit(0);
  }
}

// =============================================================================
// Query
// =============================================================================

optional<pair<CursorPos, CursorPos>> Session::currentTargetRange() const {
  if (state_.step.kind != Phase::ApproachEdit) return nullopt;
  if (!plan_) return nullopt;
  const int i = state_.step.editIndex;
  assert(i >= 0 && i < totalEdits_ && "ApproachEdit editIndex out of plan range");
  const DiffState& diff = plan_->getDiffs()[i];
  return make_pair(diff.beginPos, diff.endPos);
}

vector<Recommendation> Session::recommendations(int maxCount) const {
  if (maxCount <= 0) return {};
  if (state_.step.kind != Phase::ApproachEdit) return {};
  if (!plan_) return {};

  const int editIndex = state_.step.editIndex;
  assert(editIndex >= 0 && editIndex < totalEdits_ && "ApproachEdit editIndex out of range");

  const DiffState& diff = plan_->getDiffs()[editIndex];
  const CursorPos target = diff.beginPos;

  vector<Recommendation> recs;

  // Edit atoms at the cursor — normal-mode prefix of each planned edit sequence.
  const EditResult& editResult = plan_->getEditResults()[editIndex];
  auto editRecs = EditHandler::recommendations(editResult, state_.cursor, maxCount);
  for (auto& r : editRecs) recs.push_back(std::move(r));

  // Motion candidates toward the fixed diff-begin.
  if (target != state_.cursor) {
    auto motionRecs = MotionHandler::recommendations(
        state_.lines, state_.cursor, target, boundary_, navContext_, config_, maxCount);
    for (auto& r : motionRecs) recs.push_back(std::move(r));
  }

  std::sort(recs.begin(), recs.end(),
      [](const Recommendation& a, const Recommendation& b) {
        if (a.totalPathCost != b.totalPathCost) return a.totalPathCost < b.totalPathCost;
        return a.text < b.text;
      });
  if (static_cast<int>(recs.size()) > maxCount) recs.resize(maxCount);
  return recs;
}

// =============================================================================
// Internal helpers
// =============================================================================

Applied Session::commit(State next, bool crossedEditBoundary) {
  undo_.push_back(state_);
  redo_.clear();
  state_ = std::move(next);
  return Applied{crossedEditBoundary};
}

expected<int, Rejected> Session::requireApproachEdit(string_view action) const {
  if (state_.step.kind != Phase::ApproachEdit) {
    return unexpected(Rejected{
        string(action) + " only accepted while approaching the current edit"});
  }
  return state_.step.editIndex;
}

expected<int, Rejected> Session::requirePendingInsert(string_view action) const {
  if (state_.step.kind != Phase::PendingInsert) {
    return unexpected(Rejected{
        string(action) + " only accepted during pending insert"});
  }
  return state_.step.editIndex;
}

Applied Session::afterEditCompleted(State next) {
  const int nextEdit = next.step.editIndex + 1;
  if (nextEdit >= totalEdits_) {
    next.step = Step::completed();
  } else {
    next.step = Step::approachEdit(nextEdit);
    assert(nextEdit >= 0 && nextEdit < static_cast<int>(linesAfterEdit_.size()) &&
           "fencepost cache missing entry for advanced edit");
    next.lines = linesAfterEdit_[nextEdit];
  }
  next.acceptedRevision++;
  return commit(std::move(next), /*crossedEditBoundary=*/true);
}

// =============================================================================
// Actions
// =============================================================================

Outcome Session::applyMotion(string_view motionText) {
  auto gated = requireApproachEdit("motions");
  if (!gated) return unexpected(std::move(gated.error()));

  auto eff = MotionHandler::applyMotion(
      state_.lines, state_.cursor, motionText, navContext_);
  if (!eff.accepted) return unexpected(Rejected{std::move(eff.rejectReason)});

  State next = state_;
  next.cursor = eff.newCursor;
  next.acceptedSeq.append(eff.appendedSeq);
  next.acceptedCost = getEffort(next.acceptedSeq, config_);
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome Session::acceptCursorMove(CursorPos newCursor, string_view rawKeys) {
  auto gated = requireApproachEdit("cursor moves");
  if (!gated) return unexpected(std::move(gated.error()));

  auto eff = MotionHandler::acceptCursorMove(newCursor, rawKeys);

  State next = state_;
  next.cursor = eff.newCursor;
  if (!eff.appendedSeq.empty()) {
    next.acceptedSeq.append(eff.appendedSeq);
    next.acceptedCost = getEffort(next.acceptedSeq, config_);
  }
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome Session::applyEdit(string_view text) {
  auto gated = requireApproachEdit("edits");
  if (!gated) return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  assert(plan_ && "applyEdit after construction without a plan");
  const EditResult& editResult = plan_->getEditResults()[editIndex];
  auto eff = EditHandler::applyEdit(editResult, state_.cursor, text);
  if (!eff.accepted) return unexpected(Rejected{std::move(eff.rejectReason)});

  const int nextFencepost = editIndex + 1;
  assert(nextFencepost < static_cast<int>(linesAfterEdit_.size()) &&
         "fencepost cache missing entry for completed edit");

  State next = state_;
  next.lines = linesAfterEdit_[nextFencepost];
  next.cursor = eff.postCursor;
  next.acceptedSeq.append(text);
  next.acceptedCost = getEffort(next.acceptedSeq, config_);
  return afterEditCompleted(std::move(next));
}

Outcome Session::acceptBufferState(
    const Lines& newLines, CursorPos newCursor, string_view rawKeys) {
  auto gated = requireApproachEdit("buffer state changes");
  if (!gated) return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  assert(plan_ && "acceptBufferState after construction without a plan");
  const int nextFencepost = editIndex + 1;
  assert(nextFencepost < static_cast<int>(linesAfterEdit_.size()) &&
         "fencepost cache missing entry for completed edit");

  auto eff = EditHandler::validateBufferState(
      newLines, linesAfterEdit_[editIndex], linesAfterEdit_[nextFencepost]);
  if (!eff.accepted) return unexpected(Rejected{std::move(eff.rejectReason)});

  State next = state_;
  next.cursor = newCursor;
  // parseSequence accepts edits (unlike parseMotions), so rawKeys like "rm" or
  // "sm<Esc>" pass the gate. The gate keeps unknown bytes out of getEffort's
  // downstream tokenizer.
  if (!rawKeys.empty() && parseSequence(rawKeys)) {
    next.acceptedSeq.append(rawKeys);
    next.acceptedCost = getEffort(next.acceptedSeq, config_);
  }
  if (eff.advance) {
    next.lines = linesAfterEdit_[nextFencepost];
    return afterEditCompleted(std::move(next));
  }
  // No-op: buffer already matches current fencepost (e.g. native undo or
  // programmatic re-sync). Sync cursor + revision only.
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome Session::beginEdit(bool entersInsertMode, string_view requiredTypedText) {
  auto gated = requireApproachEdit("edits");
  if (!gated) return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  State next = state_;
  if (entersInsertMode) {
    next.step = Step::pendingInsert(editIndex, string(requiredTypedText));
    next.acceptedRevision++;
    return commit(std::move(next));
  }
  return afterEditCompleted(std::move(next));
}

Outcome Session::consumeInsertText(string_view typedChunk) {
  auto gated = requirePendingInsert("typed insert text");
  if (!gated) return unexpected(std::move(gated.error()));

  if (typedChunk.empty()) {
    return unexpected(Rejected{"typed insert chunk must be non-empty"});
  }

  string_view remaining(state_.step.remainingText);
  if (!remaining.starts_with(typedChunk)) {
    return unexpected(Rejected{"typed insert text does not match the required prefix"});
  }

  State next = state_;
  next.step.remainingText.erase(0, typedChunk.size());
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome Session::exitInsertMode() {
  auto gated = requirePendingInsert("insert-mode exit");
  if (!gated) return unexpected(std::move(gated.error()));

  if (!state_.step.remainingText.empty()) {
    return unexpected(Rejected{"cannot exit insert mode before the required text is complete"});
  }

  State next = state_;
  return afterEditCompleted(std::move(next));
}

Outcome Session::undo() {
  if (undo_.empty()) return unexpected(Rejected{"nothing to undo"});
  redo_.push_back(state_);
  state_ = undo_.back();
  undo_.pop_back();
  return Applied{};
}

Outcome Session::redo() {
  if (redo_.empty()) return unexpected(Rejected{"nothing to redo"});
  undo_.push_back(state_);
  state_ = redo_.back();
  redo_.pop_back();
  return Applied{};
}

// =============================================================================
// Debug
// =============================================================================

const char* to_string(Phase phase) {
  switch (phase) {
    case Phase::ApproachEdit:  return "ApproachEdit";
    case Phase::PendingInsert: return "PendingInsert";
    case Phase::Completed:     return "Completed";
  }
  return "Unknown";
}

}  // namespace Explore
