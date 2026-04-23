#include "Explore.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "EditHandler.h"
#include "MotionHandler.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/SequenceParser.h"
#include "Optimizer/CompositionOptimizer/CompositionStepArtifacts.h"
#include "Optimizer/EditOptimizer/EditFrontier.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionFrontier.h"

using namespace std;

namespace Explore {

namespace {

Recommendation toRecommendation(const MotionFrontierItem& item) {
  Recommendation rec;
  rec.text = item.molecule;
  rec.kind = "motion";
  rec.cost = item.cost;
  rec.totalPathCost = item.cost;
  rec.landingRow = item.landingPos.line;
  rec.landingCol = item.landingPos.col;
  return rec;
}

Recommendation toRecommendation(const EditFrontierItem& item) {
  Recommendation rec;
  rec.text = item.molecule;
  rec.kind = "edit";
  rec.cost = item.cost;
  rec.totalPathCost = item.cost;
  rec.landingRow = item.goalPos.line;
  rec.landingCol = item.goalPos.col;
  rec.typedText = item.typedText;
  return rec;
}

MotionOptimizerParams makeSingleGoalMotionParams(int maxResults) {
  CompositionOptimizerParams compParams;
  return MotionOptimizerParams{}
      .withMaxResults(maxResults)
      .withFMotionThreshold(compParams.fMotionThreshold)
      .withDirectionalPruning(compParams.useDirectionalPruning)
      .withLinePaddingAbove(compParams.motionPaddingAbove)
      .withLinePaddingBelow(compParams.motionPaddingBelow)
      .withMinCountRepeat(compParams.minPrefixCount)
      .withMaxCountRepeat(compParams.maxPrefixCount);
}

vector<Recommendation> backfillEditStartMotions(
    const Lines& lines,
    CursorPos cursor,
    const DiffState& diff,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const Config& config,
    int maxCount,
    bool allowMultiplePerPosition) {
  if (maxCount <= 0 || diff.isPureInsertion()) return {};

  CompositionOptimizerParams editParams;
  const int totalStarts = max(1, diff.deletedLines().totalPositions());
  editParams.withMaxResults(totalStarts * maxCount)
            .withMaxEditResultsPerPosition(maxCount);
  EditResult editResult = computeEditResultForDiff(diff, editParams, config);

  struct StartCandidate {
    CursorPos pos{0, 0};
    double editCost = 0.0;
  };

  vector<StartCandidate> candidates;
  Lines deletedLines = diff.deletedLines();
  candidates.reserve(static_cast<size_t>(deletedLines.totalPositions()));
  for (int lineOffset = 0; lineOffset < static_cast<int>(deletedLines.size()); lineOffset++) {
    const int bufferLine = diff.beginPos.line + lineOffset;
    const int colBase = lineOffset == 0 ? diff.beginPos.col : 0;
    for (int colOffset = 0; colOffset < deletedLines[lineOffset].effectiveSize(); colOffset++) {
      const int bufferCol = colBase + colOffset;
      const CursorPos startPos(bufferLine, bufferCol);
      if (startPos == cursor) continue;
      auto starts = editResult.resultsAt(bufferLine, bufferCol);
      if (starts.empty()) continue;
      candidates.push_back(StartCandidate{startPos, starts[0].getCost()});
    }
  }

  sort(candidates.begin(), candidates.end(), [](const StartCandidate& a, const StartCandidate& b) {
    if (a.editCost != b.editCost) return a.editCost < b.editCost;
    if (a.pos.line != b.pos.line) return a.pos.line < b.pos.line;
    return a.pos.col < b.pos.col;
  });

  MotionOptimizer motionOptimizer(config);

  vector<Recommendation> recs;
  recs.reserve(static_cast<size_t>(maxCount));
  for (const StartCandidate& candidate : candidates) {
    MotionOptimizerParams motionParams =
        makeSingleGoalMotionParams(maxCount - static_cast<int>(recs.size()));
    auto result = motionOptimizer.optimize(
        lines, cursor, candidate.pos, motionParams, "", boundary, navContext);
    for (const Result& motion : result.getResults()) {
      if (!motion.isValid()) continue;

      Recommendation rec;
      rec.text = motion.getSequence().str();
      rec.kind = "motion";
      rec.cost = motion.getCost();
      rec.totalPathCost = motion.getCost() + candidate.editCost;
      rec.landingRow = candidate.pos.line;
      rec.landingCol = candidate.pos.col;
      recs.push_back(std::move(rec));
      if (static_cast<int>(recs.size()) >= maxCount) break;
      // Under the default "dedup-by-landing" contract, each candidate
      // gets at most one motion — all motions from the optimizer for
      // this single goal land on the same `candidate.pos`, so subsequent
      // iterations would be duplicates by landing.
      if (!allowMultiplePerPosition) break;
    }
    if (static_cast<int>(recs.size()) >= maxCount) break;
  }

  return recs;
}

}

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

  totalEdits_ = plan_->totalEdits();

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
  const auto step = plan_->stepAt(i);
  return make_pair(step.diff.beginPos, step.diff.endPos);
}

vector<Recommendation> Session::recommendations(
    int maxCount,
    bool allowMultipleMotionsPerPosition,
    bool allowMultipleEditsPerPosition) const {
  if (maxCount <= 0) return {};
  if (state_.step.kind != Phase::ApproachEdit) return {};
  if (!plan_) return {};

  const int editIndex = state_.step.editIndex;
  assert(editIndex >= 0 && editIndex < totalEdits_ && "ApproachEdit editIndex out of range");

  const auto step = plan_->stepAt(editIndex);
  const DiffState& diff = step.diff;
  CompositionOptimizerParams stepParams;
  optional<JoinPlan> joinPlan = computeJoinPlanForDiff(
      diff, step.preFencepost, stepParams, config_);
  vector<Recommendation> recs;
  auto editItems = rankEditFrontier(
      EditFrontierQuery{
          .lines = state_.lines,
          .cursor = state_.cursor,
          .diff = diff,
          .maxCount = maxCount,
          .allowMultiplePerPosition = allowMultipleEditsPerPosition,
      },
      config_);
  for (const EditFrontierItem& item : editItems) {
    recs.push_back(toRecommendation(item));
  }

  if (!diff.isPureInsertion() && diff.contains(state_.cursor) &&
      static_cast<int>(recs.size()) < maxCount) {
    auto backfill = backfillEditStartMotions(
        state_.lines,
        state_.cursor,
        diff,
        boundary_,
        navContext_,
        config_,
        maxCount - static_cast<int>(recs.size()),
        allowMultipleMotionsPerPosition);
    for (auto& rec : backfill) recs.push_back(std::move(rec));
  }

  if (!diff.contains(state_.cursor)) {
    auto motionItems = rankMotionFrontier(
        MotionFrontierQuery{
            .lines = state_.lines,
            .cursor = state_.cursor,
            .targetRange = CharRange(diff.beginPos, diff.endPos),
            .boundary = boundary_,
            .navContext = navContext_,
            .maxCount = maxCount,
            .allowMultiplePerPosition = allowMultipleMotionsPerPosition,
        },
        config_);
    for (const MotionFrontierItem& item : motionItems) {
      recs.push_back(toRecommendation(item));
    }

    if (!diff.isPureInsertion() && joinPlan &&
        state_.cursor.line != joinPlan->entryLine) {
      auto jMotionItems = rankMotionFrontierToLine(
          state_.lines,
          state_.cursor,
          joinPlan->entryLine,
          boundary_,
          navContext_,
          config_,
          1);
      for (const MotionFrontierItem& item : jMotionItems) {
        recs.push_back(toRecommendation(item));
      }
    }
  }

  // Trust the optimizer's output. Each source respects
  // `allowMultiplePerPosition`, so no post-hoc filter is needed —
  // recommendations are surfaced as-generated.
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
    next.lines = plan_->stepAt(nextEdit).preFencepost;
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
  const auto step = plan_->stepAt(editIndex);
  auto eff = EditHandler::applyEdit(step.editResult, state_.cursor, text);
  if (!eff.accepted) return unexpected(Rejected{std::move(eff.rejectReason)});

  State next = state_;
  next.lines = step.postFencepost;
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
  const auto step = plan_->stepAt(editIndex);

  auto eff = EditHandler::validateBufferState(
      newLines,
      step.preFencepost,
      step.postFencepost);
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
    next.lines = step.postFencepost;
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

Outcome Session::acceptInsertExit(
    const Lines& newLines, CursorPos newCursor, string_view rawKeys) {
  auto gated = requirePendingInsert("insert-mode exit with buffer state");
  if (!gated) return unexpected(std::move(gated.error()));
  const int editIndex = *gated;
  const auto step = plan_->stepAt(editIndex);

  if (newLines != step.postFencepost) {
    return unexpected(Rejected{
        "buffer state after insert doesn't match planned fencepost"});
  }

  State next = state_;
  // Advance lines explicitly — afterEditCompleted only sets lines for the
  // *next* edit, so the current edit's post-state must be applied here.
  // MIRROR applyEdit's shape: set lines + cursor + seq/cost, then hand off.
  next.lines = step.postFencepost;
  next.cursor = newCursor;
  if (!rawKeys.empty() && parseSequence(rawKeys)) {
    next.acceptedSeq.append(rawKeys);
    next.acceptedCost = getEffort(next.acceptedSeq, config_);
  }
  return afterEditCompleted(std::move(next));
}

Outcome Session::cancelPendingInsert() {
  auto gated = requirePendingInsert("pending-insert cancel");
  if (!gated) return unexpected(std::move(gated.error()));

  // By construction, the topmost undo entry is the beginEdit snapshot
  // (ApproachEdit for this editIndex). Pop it back into live state without
  // touching redo — a rejected/abandoned insert shouldn't be user-redoable.
  assert(!undo_.empty() && "PendingInsert without a beginEdit undo snapshot");
  state_ = undo_.back();
  undo_.pop_back();
  return Applied{};
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
