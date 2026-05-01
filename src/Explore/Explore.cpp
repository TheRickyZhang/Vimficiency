#include "Explore.h"

#include <algorithm>
#include <cassert>
#include <utility>
#include <variant>

#include "EditHandler.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/SequenceParser.h"
#include "MovementHandler.h"
#include "Optimizer/CompositionOptimizer/PlannedEditArtifacts.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"

using namespace std;

namespace Explore {

namespace {

NavOptimizerParams makeSingleGoalMotionParams(int maxResults) {
  CompositionOptimizerParams compParams;
  return NavOptimizerParams{}
      .withMaxResults(maxResults)
      .withFMotionThreshold(compParams.fMotionThreshold)
      .withDirectionalPruning(compParams.useDirectionalPruning)
      .withLinePaddingAbove(compParams.navPaddingAbove)
      .withLinePaddingBelow(compParams.navPaddingBelow)
      .withMinCountRepeat(compParams.minPrefixCount)
      .withMaxCountRepeat(compParams.maxPrefixCount);
}

vector<NavFrontierItem>
backfillEditStartMovements(const Lines& lines, CursorPos cursor,
                           const DiffState& diff, const NavBoundary& boundary,
                           const NavContext& navContext, const Config& config,
                           int maxCount, bool allowMultiplePerPosition) {
  if (maxCount <= 0 || diff.isPureInsertion())
    return {};

  CompositionOptimizerParams editParams;
  const int totalStarts = max(1, diff.deletedLines().totalPositions());
  editParams.withMaxResults(totalStarts * maxCount)
      .withMaxTransformResultsPerPosition(maxCount);
  TransformResult transformResult =
      computeTransformResultForDiff(diff, editParams, config);

  struct StartCandidate {
    CursorPos pos{0, 0};
    double editCost = 0.0;
  };

  vector<StartCandidate> candidates;
  Lines deletedLines = diff.deletedLines();
  candidates.reserve(static_cast<size_t>(deletedLines.totalPositions()));
  for (int lineOffset = 0; lineOffset < static_cast<int>(deletedLines.size());
       lineOffset++) {
    const int bufferLine = diff.beginPos.line + lineOffset;
    const int colBase = lineOffset == 0 ? diff.beginPos.col : 0;
    for (int colOffset = 0;
         colOffset < deletedLines[lineOffset].effectiveSize(); colOffset++) {
      const int bufferCol = colBase + colOffset;
      const CursorPos startPos(bufferLine, bufferCol);
      if (startPos == cursor)
        continue;
      auto starts = transformResult.resultsAt(bufferLine, bufferCol);
      if (starts.empty())
        continue;
      candidates.push_back(StartCandidate{startPos, starts[0].getCost()});
    }
  }

  sort(candidates.begin(), candidates.end(),
       [](const StartCandidate& a, const StartCandidate& b) {
         if (a.editCost != b.editCost)
           return a.editCost < b.editCost;
         if (a.pos.line != b.pos.line)
           return a.pos.line < b.pos.line;
         return a.pos.col < b.pos.col;
       });

  NavOptimizer navOptimizer(config);

  vector<NavFrontierItem> items;
  items.reserve(static_cast<size_t>(maxCount));
  for (const StartCandidate& candidate : candidates) {
    NavOptimizerParams navParams =
        makeSingleGoalMotionParams(maxCount - static_cast<int>(items.size()));
    auto result = navOptimizer.optimize(lines, cursor, candidate.pos, navParams,
                                        "", boundary, navContext);
    for (const ::Result& motion : result.getResults()) {
      if (motion.getSequence().empty())
        continue;

      items.push_back(NavFrontierItem{
          FrontierItem{
              .molecule = motion.getSequence().str(),
              .goalPos = candidate.pos,
              .cost = motion.getCost(),
          },
          candidate.editCost, // projectedEditCost
      });
      if (static_cast<int>(items.size()) >= maxCount)
        break;
      // Under the default "dedup-by-landing" contract, each candidate
      // gets at most one motion — all motions from the optimizer for
      // this single goal land on the same `candidate.pos`, so subsequent
      // iterations would be duplicates by landing.
      if (!allowMultiplePerPosition)
        break;
    }
    if (static_cast<int>(items.size()) >= maxCount)
      break;
  }

  return items;
}

bool isCursorOnConcreteBufferCell(const Lines& lines, const CursorPos& pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size()))
    return false;
  const int maxCol = lines[pos.line].empty()
                         ? 0
                         : static_cast<int>(lines[pos.line].size()) - 1;
  return pos.col >= 0 && pos.col <= maxCol;
}

// Shared rawKeys handling for accept*/apply* actions that fold reported
// keys into acceptedSeq. parseSequence accepts edits (unlike parseMotions),
// so rawKeys like "rm" or "sm<Esc>" pass; the gate keeps unknown bytes out
// of getEffort's downstream tokenizer. Unparseable non-empty keys reject
// (silently dropping them undercounts effort).
std::expected<void, Rejected> appendRawKeysOrReject(State& next,
                                                    std::string_view rawKeys,
                                                    const Config& config) {
  if (rawKeys.empty())
    return {};
  if (!parseSequence(rawKeys)) {
    return std::unexpected(Rejected{"raw keys failed to parse"});
  }
  next.acceptedSeq.append(rawKeys);
  next.acceptedCost = getEffort(next.acceptedSeq, config);
  return {};
}

} // namespace

// =============================================================================
// View construction
// =============================================================================

View::View(Lines initialLines, CursorPos initialPos, Lines goalLines,
           CursorPos goalPos, NavBoundary boundary, NavContext navContext,
           Config config, string_view userSequence)
    : goalLines_(std::move(goalLines)), goalPos_(goalPos),
      boundary_(std::move(boundary)), navContext_(navContext),
      config_(std::move(config)) {

  state_.lines = initialLines;
  state_.cursor = initialPos;

  // MIRROR: composition optimizer computes the plan we walk through. This
  // call's inputs and semantics must track CompositionOptimizer::optimize.
  // Pure-motion sessions (initialLines == goalLines_) flow through here too,
  // producing a 0-edit plan whose only nav target is goalPos_.
  CompositionOptimizer opt(config_);
  plan_ = opt.optimize(state_.lines, initialPos, goalLines_, goalPos_,
                       compositionParams_, userSequence, boundary_,
                       navContext_);

  totalEdits_ = plan_.totalEdits();
  state_.phase = phaseForEditCursor(0, state_.cursor);
}

// =============================================================================
// Query
// =============================================================================

optional<pair<CursorPos, CursorPos>> View::currentTargetRange() const {
  // Insert mode: UI doesn't render a target range during typing.
  if (std::holds_alternative<Insert>(state_.phase)) return nullopt;

  const int i = phaseIndex(state_.phase);
  CharRange r = plan_.getPlan().navTarget(i);
  return make_pair(r.begin, r.end);
}

bool View::isCompleted() const {
  auto* nav = std::get_if<Navigate>(&state_.phase);
  return nav && nav->index == totalEdits_ && state_.cursor == goalPos_;
}

vector<Suggestion>
View::recommendations(int maxCount, bool allowMultipleMovementsPerPosition,
                      bool allowMultipleEditsPerPosition) const {
  if (maxCount <= 0)
    return {};
  if (std::holds_alternative<Insert>(state_.phase))
    return {};

  const int phaseIdx = phaseIndex(state_.phase);

  // Navigate(i) — including the post-final-edit Navigate(totalEdits) —
  // surfaces only motion suggestions toward navTarget(i). The pure-motion
  // (E=0) and "final approach" (i==totalEdits) cases share this branch.
  if (std::holds_alternative<Navigate>(state_.phase)) {
    CharRange target = plan_.getPlan().navTarget(phaseIdx);
    auto navItems = rankNavFrontier(
        NavFrontierQuery{
            FrontierQuery{
                .lines = state_.lines,
                .cursor = state_.cursor,
                .maxCount = maxCount,
                .allowMultiplePerPosition = allowMultipleMovementsPerPosition,
            },
            target,
            boundary_,
            navContext_,
        },
        config_);
    vector<Suggestion> recs;
    recs.reserve(navItems.size());
    for (auto& item : navItems)
      recs.emplace_back(std::move(item));
    return recs;
  }

  // Transform(i): edit alternatives + (optional) backfill + nav-to-diff +
  // (optional) join-line hint. i is always in [0, totalEdits_) by variant
  // invariant, so the plan lookup is unconditional.
  const int editIndex = phaseIdx;
  assert(editIndex >= 0 && editIndex < totalEdits_ &&
         "Transform editIndex out of range");

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  const DiffState& diff = plannedEdit.diff;
  optional<JoinPlan> joinPlan = computeJoinPlanForDiff(
      diff, plannedEdit.preFencepost, compositionParams_, config_);
  vector<Suggestion> recs;
  auto transformItems = rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = state_.lines,
              .cursor = state_.cursor,
              .maxCount = maxCount,
              .allowMultiplePerPosition = allowMultipleEditsPerPosition,
          },
          diff, // diff
      },
      config_);
  for (auto& item : transformItems)
    recs.emplace_back(std::move(item));

  if (!diff.isPureInsertion() && diff.contains(state_.cursor) &&
      static_cast<int>(recs.size()) < maxCount) {
    auto backfill = backfillEditStartMovements(
        state_.lines, state_.cursor, diff, boundary_, navContext_, config_,
        maxCount - static_cast<int>(recs.size()),
        allowMultipleMovementsPerPosition);
    for (auto& item : backfill)
      recs.emplace_back(std::move(item));
  }

  if (!diff.contains(state_.cursor)) {
    auto navItems = rankNavFrontier(
        NavFrontierQuery{
            FrontierQuery{
                .lines = state_.lines,
                .cursor = state_.cursor,
                .maxCount = maxCount,
                .allowMultiplePerPosition = allowMultipleMovementsPerPosition,
            },
            CharRange(diff.beginPos, diff.endPos), // targetRange
            boundary_,                             // boundary
            navContext_,                           // navContext
        },
        config_);
    for (auto& item : navItems)
      recs.emplace_back(std::move(item));

    if (!diff.isPureInsertion() && joinPlan &&
        state_.cursor.line != joinPlan->entryLine) {
      auto jMotionItems = rankNavFrontierToLine(state_.lines, state_.cursor,
                                                joinPlan->entryLine, boundary_,
                                                navContext_, config_, 1);
      for (auto& item : jMotionItems)
        recs.emplace_back(std::move(item));
    }
  }

  // Trust the optimizer's output. Each source respects
  // `allowMultiplePerPosition`, so no post-hoc filter is needed —
  // recommendations are surfaced as-generated.
  if (static_cast<int>(recs.size()) > maxCount)
    recs.resize(maxCount);
  return recs;
}

// =============================================================================
// Internal helpers
// =============================================================================

Applied View::commit(State next, bool crossedEditBoundary) {
  undo_.push_back(state_);
  redo_.clear();
  state_ = std::move(next);
  return Applied{crossedEditBoundary};
}

Phase View::phaseForEditCursor(int editIndex, CursorPos cursor) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "phaseForEditCursor index out of plan range");
  // Post-final-edit nav segment — the only target is goalPos, no edit to
  // enter. Pure-motion sessions (E=0) start here too.
  if (editIndex == totalEdits_)
    return Navigate{editIndex};
  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  const DiffState& diff = plannedEdit.diff;
  if (!diff.isPureInsertion() && diff.contains(cursor))
    return Transform{editIndex};
  if (diff.isPureInsertion() && cursor == diff.beginPos)
    return Transform{editIndex};
  return Navigate{editIndex};
}

bool View::movementStaysInTransformRange(CursorPos cursor) const {
  auto* t = std::get_if<Transform>(&state_.phase);
  if (!t) return true;
  const auto plannedEdit = plan_.plannedEditAt(t->index);
  if (plannedEdit.diff.isPureInsertion())
    return cursor == plannedEdit.diff.beginPos;
  return plannedEdit.diff.contains(cursor);
}

expected<int, Rejected>
View::requireNavigateOrTransform(string_view action) const {
  if (!std::holds_alternative<Navigate>(state_.phase) &&
      !std::holds_alternative<Transform>(state_.phase)) {
    return unexpected(Rejected{
        string(action) + " only accepted while navigating or transforming"});
  }
  return phaseIndex(state_.phase);
}

expected<int, Rejected>
View::requireTransform(string_view action) const {
  auto* t = std::get_if<Transform>(&state_.phase);
  if (!t) {
    return unexpected(
        Rejected{string(action) + " only accepted while transforming"});
  }
  return t->index;
}

expected<int, Rejected> View::requireInsert(string_view action) const {
  auto* i = std::get_if<Insert>(&state_.phase);
  if (!i) {
    return unexpected(
        Rejected{string(action) + " only accepted during insert"});
  }
  return i->index;
}

Applied View::afterEditCompleted(State next, int editIndex) {
  const int nextEdit = editIndex + 1;
  // Always transition to phaseForEditCursor(nextEdit, ...). For
  // nextEdit == totalEdits_ that returns Navigate{totalEdits_} — the
  // post-final-edit nav segment. Completion is the derived predicate
  // `isCompleted()` (Navigate at totalEdits_ with cursor at goalPos).
  if (nextEdit < totalEdits_) {
    next.lines = plan_.plannedEditAt(nextEdit).preFencepost;
  }
  next.phase = phaseForEditCursor(nextEdit, next.cursor);
  next.acceptedRevision++;
  return commit(std::move(next), /*crossedEditBoundary=*/true);
}

// =============================================================================
// Actions
// =============================================================================

Outcome View::applyMovement(string_view movementText) {
  auto gated = requireNavigateOrTransform("motions");
  if (!gated)
    return unexpected(std::move(gated.error()));

  auto eff = MovementHandler::applyMovement(
      state_.lines, state_.cursor, movementText, boundary_, navContext_);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->newCursor;
  if (!movementStaysInTransformRange(next.cursor)) {
    return unexpected(
        Rejected{"transform movement left the current edit range"});
  }
  next.acceptedSeq.append(std::move(eff->appendedSeq));
  next.acceptedCost = getEffort(next.acceptedSeq, config_);
  next.phase = phaseForEditCursor(*gated, next.cursor);
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome View::acceptCursorMove(CursorPos newCursor, string_view rawKeys) {
  auto gated = requireNavigateOrTransform("cursor moves");
  if (!gated)
    return unexpected(std::move(gated.error()));

  auto eff = MovementHandler::acceptCursorMove(
      state_.lines, state_.cursor, newCursor, rawKeys, boundary_, navContext_);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->newCursor;
  if (!movementStaysInTransformRange(next.cursor)) {
    return unexpected(
        Rejected{"transform movement left the current edit range"});
  }
  if (!eff->appendedSeq.empty()) {
    next.acceptedSeq.append(std::move(eff->appendedSeq));
    next.acceptedCost = getEffort(next.acceptedSeq, config_);
  }
  next.phase = phaseForEditCursor(*gated, next.cursor);
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome View::applyEdit(string_view text) {
  auto gated = requireTransform("edits");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto eff =
      EditHandler::applyEdit(plannedEdit.transformResult, state_.cursor, text);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.lines = plannedEdit.postFencepost;
  next.cursor = eff->postCursor;
  next.acceptedSeq.append(text);
  next.acceptedCost = getEffort(next.acceptedSeq, config_);
  return afterEditCompleted(std::move(next), editIndex);
}

Outcome View::acceptBufferState(const Lines& newLines, CursorPos newCursor,
                                string_view rawKeys) {
  auto gated = requireNavigateOrTransform("buffer state changes");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;
  if (editIndex == totalEdits_) {
    // Post-final-edit nav segment — no edit to apply or revert via buffer state.
    return unexpected(
        Rejected{"buffer state changes not accepted post-final-edit"});
  }

  const auto plannedEdit = plan_.plannedEditAt(editIndex);

  auto eff = EditHandler::validateBufferState(newLines, plannedEdit.preFencepost,
                                              plannedEdit.postFencepost);
  if (!eff)
    return unexpected(std::move(eff.error()));
  if (!isCursorOnConcreteBufferCell(newLines, newCursor)) {
    return unexpected(
        Rejected{"buffer state reported an invalid cursor position"});
  }

  State next = state_;
  next.cursor = newCursor;
  if (auto e = appendRawKeysOrReject(next, rawKeys, config_); !e) {
    return unexpected(std::move(e.error()));
  }
  if (eff->advance) {
    next.lines = plannedEdit.postFencepost;
    return afterEditCompleted(std::move(next), editIndex);
  }
  // No-op: buffer already matches current fencepost (e.g. native undo or
  // programmatic re-sync). Sync cursor + revision only.
  next.phase = phaseForEditCursor(editIndex, next.cursor);
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome View::beginInsert() {
  auto gated = requireNavigateOrTransform("insert-mode edit");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;
  if (editIndex == totalEdits_) {
    return unexpected(
        Rejected{"insert-mode edit not accepted post-final-edit"});
  }

  State next = state_;
  next.phase = Insert{editIndex};
  next.acceptedRevision++;
  return commit(std::move(next));
}

Outcome View::acceptInsertExit(const Lines& newLines, CursorPos newCursor,
                               string_view rawKeys) {
  auto gated = requireInsert("insert-mode exit with buffer state");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;
  const auto plannedEdit = plan_.plannedEditAt(editIndex);

  if (newLines != plannedEdit.postFencepost) {
    return unexpected(
        Rejected{"buffer state after insert doesn't match planned fencepost"});
  }
  if (!isCursorOnConcreteBufferCell(newLines, newCursor)) {
    return unexpected(
        Rejected{"buffer state reported an invalid cursor position"});
  }

  State next = state_;
  // Advance lines explicitly — afterEditCompleted only sets lines for the
  // *next* edit, so the current edit's post-state must be applied here.
  // MIRROR applyEdit's shape: set lines + cursor + seq/cost, then hand off.
  next.lines = plannedEdit.postFencepost;
  next.cursor = newCursor;
  if (auto e = appendRawKeysOrReject(next, rawKeys, config_); !e) {
    return unexpected(std::move(e.error()));
  }
  return afterEditCompleted(std::move(next), editIndex);
}

Outcome View::cancelInsert() {
  auto gated = requireInsert("insert cancel");
  if (!gated)
    return unexpected(std::move(gated.error()));

  // By construction, the topmost undo entry is the beginInsert snapshot
  // (Navigate/Transform for this editIndex). Pop it back into live state without
  // touching redo — a rejected/abandoned insert shouldn't be user-redoable.
  assert(!undo_.empty() && "Insert without a beginInsert undo snapshot");
  state_ = undo_.back();
  undo_.pop_back();
  return Applied{};
}

Outcome View::undo() {
  if (undo_.empty())
    return unexpected(Rejected{"nothing to undo"});
  redo_.push_back(state_);
  state_ = undo_.back();
  undo_.pop_back();
  return Applied{};
}

Outcome View::redo() {
  if (redo_.empty())
    return unexpected(Rejected{"nothing to redo"});
  undo_.push_back(state_);
  state_ = redo_.back();
  redo_.pop_back();
  return Applied{};
}

// =============================================================================
// Debug
// =============================================================================

// phaseKindName overloads live in Explore.h alongside Phase.

} // namespace Explore
