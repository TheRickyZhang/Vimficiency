#include "Explore.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>
#include <variant>

#include "EditHandler.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "MovementHandler.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/CompositionOptimizer/CompositionFrontier.h"
#include "Optimizer/NavOptimizer/NavFrontier.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/TransformOptimizer/TransformFrontier.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimOptions.h"

using namespace std;

namespace Explore {

namespace {

// Raw keys are evidence of how Vim reached the observed state; they are not
// allowed to veto a state Vim already produced.
void appendRawKeys(State& next,
                   std::string_view rawKeys,
                   const Config& config) {
  if (rawKeys.empty())
    return;
  next.seq.append(rawKeys);
  next.cost = getEffort(next.seq, config);
}

bool isHistoryCheckpoint(const State& state) {
  return !std::holds_alternative<Insert>(state.phase) &&
         !state.hasPartialEditSpan;
}

KeyedSequence buildInsertModeTypedSequence(const DiffState& diff,
                                            const Lines& lines) {
  if (diff.isPureInsertion() && diff.isNewLineInsertion() &&
      diff.beginPos.col == 0 && diff.beginPos.line > 0) {
    const int targetLine = diff.beginPos.line - 1;
    const std::string_view sourceIndent = VimOptions::autoindent()
        ? leadingWhitespace(lines[targetLine])
        : std::string_view{};
    Lines insertLines = Lines::unflatten(std::string(diff.insertedTextBody()));
    return buildTypedCommands(insertLines, sourceIndent);
  }

  const int line = diff.beginPos.line;
  const int col = diff.beginPos.col;
  std::string_view lineText(lines[line].data(), lines[line].size());
  Lines insertLines = Lines::unflatten(diff.insertedText);

  if (diff.isPureInsertion()) {
    const int fnb = VimCore::firstNonBlankColInLine(lines[line]);
    const int lineLen = static_cast<int>(lines[line].size());
    if (col == fnb)
      return buildTypedCommands(
          insertLines, "", lineText.substr(0, fnb), lineText.substr(fnb));
    if (col == lineLen)
      return buildTypedCommands(insertLines, "", lines[line]);
    return buildTypedCommands(
        insertLines, "", lineText.substr(0, col), lineText.substr(col));
  }

  return buildTypedCommands(insertLines, "", lines[line].substr(0, col));
}

std::string deriveInsertModeTypedText(const DiffState& diff,
                                       const Lines& lines) {
  if (diff.isPureDeletion()) return "";

  KeyedSequence ks = buildInsertModeTypedSequence(diff, lines);

  std::string_view raw = ks.seq.view();
  static constexpr std::string_view ESC_STR = "<Esc>";
  if (raw.size() >= ESC_STR.size() &&
      raw.substr(raw.size() - ESC_STR.size()) == ESC_STR) {
    raw.remove_suffix(ESC_STR.size());
  }
  return std::string(raw);
}

CursorPos insertExitCursor(const DiffState& diff) {
  bool newlineHandledByModeEntry =
      diff.isPureInsertion() && diff.isNewLineInsertion() &&
      diff.beginPos.col == 0 && diff.beginPos.line > 0;
  Lines inserted =
      newlineHandledByModeEntry
          ? Lines::unflatten(std::string(diff.insertedTextBody()))
          : diff.insertedLines();
  std::string_view suffix =
      newlineHandledByModeEntry ? "" : diff.boundary.suffix();
  return typedCommandsExitCursor(diff.beginPos, inserted, suffix);
}

bool containsInsertCursor(const Lines& lines, CursorPos pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size()))
    return false;
  return pos.col >= 0 && pos.col <= static_cast<int>(lines[pos.line].size());
}

std::optional<DiffState> insertionResidualBetween(
    const Lines& from,
    const Lines& to) {
  auto diff = DiffText::calculateContiguousResidualDiff(from, to);
  if (!diff || !diff->isPureInsertion()) return std::nullopt;
  return diff;
}

DiffState deletionPrefixDiff(const DiffState& diff) {
  return DiffState(
      diff.beginPos,
      diff.endPos,
      diff.deletedText,
      "",
      diff.boundary);
}

CharRange cursorPointRange(CursorPos pos) {
  return CharRange{pos, CursorPos(pos.line, pos.col + 1)};
}

void appendUniqueSuggestions(vector<Suggestion>& dest, vector<Suggestion>&& src) {
  for (auto& suggestion : src) {
    const bool duplicate = any_of(dest.begin(), dest.end(),
        [&](const Suggestion& item) { return item.token == suggestion.token; });
    if (!duplicate) dest.push_back(std::move(suggestion));
  }
}

bool sameResult(const Result& a, const Result& b) {
  return a.getSequence().view() == b.getSequence().view() &&
         a.getCost() == b.getCost();
}

bool sameResultList(const vector<Result>& a, const vector<Result>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!sameResult(a[i], b[i])) return false;
  }
  return true;
}

bool sameTransformResult(const TransformResult& a, const TransformResult& b) {
  if (a.getGoalPos() != b.getGoalPos()) return false;
  if (a.startPositions() != b.startPositions()) return false;
  if (a.hasResultGoals() != b.hasResultGoals()) return false;
  const auto& ar = a.getResults();
  const auto& br = b.getResults();
  if (ar.size() != br.size()) return false;
  for (size_t i = 0; i < ar.size(); ++i) {
    if (!sameResultList(ar[i], br[i])) return false;
  }
  for (CursorPos start : a.startPositions()) {
    auto aBucket = a.resultsAt(start.line, start.col);
    auto bBucket = b.resultsAt(start.line, start.col);
    if (aBucket.size() != bBucket.size()) return false;
    for (size_t j = 0; j < aBucket.size(); j++) {
      if (a.goalPosAt(start.line, start.col, j) !=
          b.goalPosAt(start.line, start.col, j)) {
        return false;
      }
    }
  }
  return true;
}

bool sameDiff(const DiffState& a, const DiffState& b) {
  return a.beginPos == b.beginPos &&
         a.endPos == b.endPos &&
         a.deletedText == b.deletedText &&
         a.insertedText == b.insertedText;
}

bool sameCompositionResult(const CompositionResult& a,
                           const CompositionResult& b) {
  const CompositionPlan& ap = a.getPlan();
  const CompositionPlan& bp = b.getPlan();
  if (ap.finalGoalPos != bp.finalGoalPos) return false;
  if (ap.fenceposts != bp.fenceposts) return false;
  if (ap.diffs.size() != bp.diffs.size()) return false;
  for (size_t i = 0; i < ap.diffs.size(); ++i) {
    if (!sameDiff(ap.diffs[i], bp.diffs[i])) return false;
    if (!sameTransformResult(
            a.plannedEditAt(static_cast<int>(i)).transformResult,
            b.plannedEditAt(static_cast<int>(i)).transformResult)) {
      return false;
    }
  }
  return sameResultList(a.getResults(), b.getResults());
}

} // namespace

// =============================================================================
// View construction
// =============================================================================

View::View(Lines initialLines, CursorPos initialPos, Lines goalLines,
           CursorPos goalPos, NavBoundary boundary, NavContext navContext,
           Config config, string_view userSequence,
           CompositionOptimizerParams compositionParams)
    : initialLines_(std::move(initialLines)), initialPos_(initialPos),
      goalLines_(std::move(goalLines)), goalPos_(goalPos),
      boundary_(std::move(boundary)), navContext_(navContext),
      config_(std::move(config)),
      userSequence_(userSequence),
      compositionParams_(std::move(compositionParams)) {

  resetToInitial();
  installPlan(computePlan(compositionParams_));
  state_.phase = phaseForCursor(0, state_.lines, state_.cursor);
}

// =============================================================================
// Query
// =============================================================================

pair<CursorPos, CursorPos> View::currentTargetRange() const {
  CharRange r = plan_.getPlan().navTarget(phaseIndex(state_.phase));
  return {r.begin, r.end};
}

bool View::isCompleted() const {
  auto* nav = std::get_if<Navigate>(&state_.phase);
  return nav && nav->index == totalEdits_ && state_.cursor == goalPos_;
}

View::HeaderRows View::headerRows() const {
  HeaderRows out;
  out.explored = rowsFromEditSpans(
      state_.seq,
      std::span<const EditSequenceSpan>(
          state_.editSpans.spans.data(), state_.editSpans.size));

  const auto& results = plan_.getResults();
  assert(results.size() == optimalEditSpans_.size() &&
         "optimal results must align with traced edit spans");
  out.optimal.reserve(results.size());
  for (size_t i = 0; i < results.size(); ++i) {
    out.optimal.push_back(rowsFromEditSpans(
        results[i].getSequence().view(), optimalEditSpans_[i]));
  }
  return out;
}

vector<Suggestion> View::recommendations(
    int maxCount,
    const OptimizerParamOverrides* overrides,
    SuggestionSortMode sortMode) const {
  return std::visit(overload{
      [&](const Navigate& nav) {
        return recommendNavigate(nav.index, maxCount, overrides, sortMode);
      },
      [&](const Transform& transform) {
        return recommendTransform(transform.index, maxCount, overrides, sortMode);
      },
      [&](const Insert& insert) {
        return recommendInsert(insert.index, maxCount);
      },
  }, state_.phase);
}

PlanReconfigureResult View::reconfigurePlan(
    CompositionOptimizerParams compositionParams) {
  CompositionTraceResult traced = computePlan(compositionParams);
  const bool resetState = !sameCompositionResult(plan_, traced.result);

  compositionParams_ = std::move(compositionParams);
  installPlan(std::move(traced));

  if (resetState) {
    resetToInitial();
    state_.phase = phaseForCursor(0, state_.lines, state_.cursor);
  }
  return PlanReconfigureResult{resetState};
}

// =============================================================================
// Internal helpers
// =============================================================================

CompositionTraceResult View::computePlan(
    CompositionOptimizerParams compositionParams) const {
  // Explore opts into the traced variant because it needs plan-aligned edit
  // spans to render the Optimal-N header columns.
  CompositionOptimizer opt(config_);
  return opt.optimizeWithEditSpans(
      initialLines_, initialPos_, goalLines_, goalPos_,
      compositionParams, userSequence_, boundary_, navContext_);
}

void View::installPlan(CompositionTraceResult traced) {
  plan_ = std::move(traced.result);
  optimalEditSpans_ = std::move(traced.editSpansByResult);
  totalEdits_ = plan_.totalEdits();
}

void View::resetToInitial() {
  state_ = State{};
  state_.lines = initialLines_;
  state_.cursor = initialPos_;
  undo_.clear();
  redo_.clear();
}

Outcome View::commit(State next) {
  // Insert and partial edit states are internal pieces of one Vim change.
  if (isHistoryCheckpoint(state_)) {
    undo_.push_back(state_);
  }
  redo_.clear();
  state_ = std::move(next);
  return {};
}

FrontierQuery View::makeFrontierQuery(
    int maxCount,
    const OptimizerParamOverrides* overrides,
    SuggestionSortMode sortMode) const {
  return FrontierQuery{
      .lines = state_.lines,
      .cursor = state_.cursor,
      .seq = state_.seq,
      .maxCount = maxCount,
      .sortMode = sortMode,
      .overrides = overrides,
  };
}

vector<Suggestion> View::recommendNavigate(
    int navIndex,
    int maxCount,
    const OptimizerParamOverrides* overrides,
    SuggestionSortMode sortMode) const {
  FrontierQuery frontierQuery = makeFrontierQuery(maxCount, overrides, sortMode);
  vector<Suggestion> nav;

  if (navIndex < totalEdits_) {
    for (CursorPos start : executableEditStartPositions(navIndex, state_.lines)) {
      appendUniqueSuggestions(nav, rankNavFrontier(
          NavFrontierQuery{
            frontierQuery,
            cursorPointRange(start),
            boundary_,
            navContext_,
        }, config_));
    }

    const DiffState& diff = plan_.plannedEditAt(navIndex).diff;
    vector<Suggestion> composition = rankCompositionFrontier(
        CompositionFrontierQuery{frontierQuery, diff, boundary_, navContext_},
        config_);
    appendUniqueSuggestions(nav, std::move(composition));
  } else {
    CharRange target = plan_.getPlan().navTarget(navIndex);
    nav = rankNavFrontier(
        NavFrontierQuery{
          frontierQuery,
          target.isEmpty() ? cursorPointRange(target.begin) : target,
          boundary_,
          navContext_,
      }, config_);
  }

  sortAndCapSuggestions(nav, maxCount, sortMode);
  return nav;
}

vector<Suggestion> View::recommendTransform(
    int editIndex,
    int maxCount,
    const OptimizerParamOverrides* overrides,
    SuggestionSortMode sortMode) const {
  assert(editIndex >= 0 && editIndex < totalEdits_ && "Transform editIndex out of range");
  assert(hasExecutableEditStartAt(editIndex, state_.lines, state_.cursor) &&
         "Transform phase requires an executable edit start");

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = DiffText::calculateContiguousResidualDiff(
      state_.lines, plannedEdit.postFencepost);

  if (currentResidual && currentResidual->isPureInsertion()) {
    return rankTransformFrontier(
        TransformFrontierQuery{makeFrontierQuery(maxCount, overrides, sortMode), *currentResidual},
        config_);
  }

  vector<Suggestion> primary = rankTransformFrontier(
      TransformFrontierQuery{makeFrontierQuery(maxCount, overrides, sortMode), plannedEdit.diff},
      config_);
  if (!plannedEdit.diff.isReplacement() &&
      !(currentResidual && currentResidual->isReplacement())) {
    return primary;
  }

  // Extra Explore candidates expose delete-then-insert without changing the
  // optimizer's canonical preference for one change command.
  const DiffState& deletionBase =
      (currentResidual && currentResidual->isReplacement())
          ? *currentResidual
          : plannedEdit.diff;
  DiffState deletionPrefix = deletionPrefixDiff(deletionBase);
  vector<Suggestion> alternatives = rankTransformFrontier(
      TransformFrontierQuery{makeFrontierQuery(maxCount, overrides, sortMode), deletionPrefix},
      config_);
  TransformOptimizerParams transformParams =
      OptimizerParamOverrides::resolved<TransformOptimizerParams>(overrides);
  const double residualInsertDistance =
      textDistanceEstimate(deletionBase.insertedLines());
  for (auto& alt : alternatives) {
    const bool duplicate = any_of(primary.begin(), primary.end(),
        [&](const Suggestion& item) { return item.token == alt.token; });
    if (duplicate) continue;
    updateSuggestionMetrics(
        alt, alt.costDiff, alt.distance + residualInsertDistance,
        transformParams.effortWeight, transformParams.distanceWeight);
    primary.push_back(std::move(alt));
  }
  sortAndCapSuggestions(primary, maxCount, sortMode);
  return primary;
}

vector<Suggestion> View::recommendInsert(int editIndex, int maxCount) const {
  if (maxCount <= 0) return {};
  assert(editIndex >= 0 && editIndex < totalEdits_ &&
         "Insert editIndex out of range");

  // state_.lines is the pre-edit fencepost during Insert (afterEditCompleted
  // hasn't run yet) unless the user entered Insert after a Normal-mode
  // deletion prefix. In that case the physical buffer is already the
  // deletion intermediate, so derive the typed tail from the residual diff.
  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = insertionResidualBetween(
      state_.lines, plannedEdit.postFencepost);
  const DiffState& insertDiff = currentResidual
      ? *currentResidual
      : plannedEdit.diff;
  std::string typedText =
      deriveInsertModeTypedText(insertDiff, state_.lines);
  if (typedText.empty()) return {};

  // Running effort is contextual across the accepted-sequence boundary.
  RunningEffort acceptedEffort(
      globalSequenceToKeys().tokenize(state_.seq), config_);
  const double acceptedCost = acceptedEffort.getEffort(config_);
  RunningEffort candidate(globalSequenceToKeys().tokenize(typedText), config_);
  RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate);
  const double costDiff = merged.getEffort(config_) - acceptedCost;

  Suggestion suggestion{
      .token = Token{typedText},
      .landingPos = currentResidual
          ? insertExitCursor(insertDiff)
          : plannedEdit.transformResult.getGoalPos(),
  };
  suggestion.literalText = insertDiff.insertedText;
  updateSuggestionMetrics(suggestion, costDiff, 0.0, 1.0, 1.0);
  return {std::move(suggestion)};
}

bool View::hasExecutableEditStartAt(
    int editIndex,
    const Lines& lines,
    CursorPos cursor) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "edit-start query index out of plan range");
  if (editIndex == totalEdits_) return false;

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = insertionResidualBetween(
      lines, plannedEdit.postFencepost);
  if (currentResidual && currentResidual->isPureInsertion()) {
    return cursor == currentResidual->beginPos &&
           containsInsertCursor(lines, cursor);
  }
  return !plannedEdit.transformResult.resultsAt(cursor.line, cursor.col).empty();
}

vector<CursorPos> View::executableEditStartPositions(
    int editIndex,
    const Lines& lines) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "edit-start query index out of plan range");
  if (editIndex == totalEdits_) return {};

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = insertionResidualBetween(
      lines, plannedEdit.postFencepost);
  if (currentResidual && currentResidual->isPureInsertion()) {
    return {currentResidual->beginPos};
  }
  return plannedEdit.transformResult.startPositions();
}

Phase View::phaseForCursor(int editIndex, const Lines& lines, CursorPos cursor) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "phaseForCursor index out of plan range");
  if (hasExecutableEditStartAt(editIndex, lines, cursor))
    return Transform{editIndex};
  return Navigate{editIndex};
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

expected<int, Rejected>
View::requirePlannedEditTarget(string_view action) const {
  auto gated = requireNavigateOrTransform(action);
  if (!gated)
    return unexpected(std::move(gated.error()));

  if (*gated == totalEdits_) {
    return unexpected(
        Rejected{string(action) + " not accepted post-final-edit"});
  }

  return *gated;
}

expected<int, Rejected> View::requireInsert(string_view action) const {
  auto* i = std::get_if<Insert>(&state_.phase);
  if (!i) {
    return unexpected(
        Rejected{string(action) + " only accepted during insert"});
  }
  return i->index;
}

Outcome View::afterEditCompleted(State next, int editIndex) {
  const int nextEdit = editIndex + 1;
  next.lines = plan_.getPlan().fencepostAt(nextEdit);
  next.hasPartialEditSpan = false;
  next.phase = phaseForCursor(nextEdit, next.lines, next.cursor);
  return commit(std::move(next));
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
  next.seq.append(std::move(eff->appendedSeq));
  next.cost = getEffort(next.seq, config_);
  next.phase = phaseForCursor(*gated, next.lines, next.cursor);
  return commit(std::move(next));
}

Outcome View::acceptCursorMove(CursorPos newCursor, string_view rawKeys) {
  auto gated = requireNavigateOrTransform("cursor moves");
  if (!gated)
    return unexpected(std::move(gated.error()));

  auto eff = MovementHandler::acceptCursorMove(
      state_.lines, newCursor, rawKeys, boundary_);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->newCursor;
  if (!eff->appendedSeq.empty()) {
    next.seq.append(std::move(eff->appendedSeq));
    next.cost = getEffort(next.seq, config_);
  }
  next.phase = phaseForCursor(*gated, next.lines, next.cursor);
  return commit(std::move(next));
}

Outcome View::applyEdit(string_view text) {
  // Relaxed from Transform-only to any planned-edit-target phase so that
  // composition-frontier tokens (`A`, `ci(`, `J`, etc.) — whose activation
  // regions go beyond ordinary Transform start columns — are also applicable
  // programmatically. EditHandler::applyEdit owns the planner-scope
  // validation across Transform and Composition lanes.
  auto gated = requirePlannedEditTarget("edits");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto eff = EditHandler::applyEdit(
      plannedEdit, state_.lines, state_.cursor, text,
      compositionParams_, config_);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->postCursor;
  const uint32_t spanBegin = static_cast<uint32_t>(next.seq.size());
  next.seq.append(text);
  const uint32_t spanEnd = static_cast<uint32_t>(next.seq.size());
  next.cost = getEffort(next.seq, config_);

  if (eff->entersInsert) {
    // Insert-entering composition token (`A`, `ci(`, ...): apply the
    // structural effect and transition to Insert phase. The typed payload
    // is supplied later via acceptInsertExit / acceptSnapshot.
    next.lines = std::move(eff->postLines);
    next.phase = Insert{editIndex};
    next.hasPartialEditSpan = true;
    next.partialEditSpanBegin = spanBegin;
    return commit(std::move(next));
  }

  if (eff->reachesPostFencepost) {
    // Normal-mode token that completes the planned edit (existing Transform
    // path, plus pure-deletion text objects and J-as-completion).
    next.editSpans.push(EditSequenceSpan{spanBegin, spanEnd});
    next.hasPartialEditSpan = false;
    return afterEditCompleted(std::move(next), editIndex);
  }

  // Normal-mode token that made partial progress (e.g. J that did not reach
  // the fencepost). Keep the simulated buffer/cursor and recompute phase.
  next.lines = std::move(eff->postLines);
  next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
  if (!next.hasPartialEditSpan) {
    next.hasPartialEditSpan = true;
    next.partialEditSpanBegin = spanBegin;
  }
  return commit(std::move(next));
}

Outcome View::acceptBufferState(const Lines& newLines, CursorPos newCursor,
                                string_view rawKeys) {
  auto gated = requirePlannedEditTarget("buffer state changes");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  const auto plannedEdit = plan_.plannedEditAt(editIndex);

  auto eff = EditHandler::validateBufferState(newLines, plannedEdit.preFencepost,
                                              plannedEdit.postFencepost);
  if (!eff)
    return unexpected(std::move(eff.error()));
  if (!newLines.contains(newCursor)) {
    return unexpected(
        Rejected{"buffer state reported an invalid cursor position"});
  }

  State next = state_;
  next.cursor = newCursor;
  // Capture span begin before appending raw keys so the edit's start byte is
  // exactly where the rawKeys (which performed this edit) begin in seq.
  const uint32_t spanBegin = next.hasPartialEditSpan
      ? next.partialEditSpanBegin
      : static_cast<uint32_t>(next.seq.size());
  appendRawKeys(next, rawKeys, config_);
  if (eff->advance) {
    const uint32_t spanEnd = static_cast<uint32_t>(next.seq.size());
    next.editSpans.push(EditSequenceSpan{spanBegin, spanEnd});
    next.hasPartialEditSpan = false;
    return afterEditCompleted(std::move(next), editIndex);
  }
  // No-op: buffer already matches current fencepost (e.g. native undo or
  // programmatic re-sync). Sync cursor only — no span pushed because no
  // planned edit advanced.
  next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
  return commit(std::move(next));
}

Outcome View::beginInsert() {
  auto gated = requirePlannedEditTarget("insert-mode edit");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  State next = state_;
  next.phase = Insert{editIndex};
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
  if (!newLines.contains(newCursor)) {
    return unexpected(
        Rejected{"buffer state reported an invalid cursor position"});
  }

  State next = state_;
  // MIRROR applyEdit's shape: set cursor + seq/cost, then hand off to the
  // uniform fencepost advancement path. Lua keeps the full insert command in
  // rawKeys (`s`/`cl`/etc. + typed text + `<Esc>`), so this span covers the
  // complete planned edit.
  next.cursor = newCursor;
  const uint32_t spanBegin = next.hasPartialEditSpan
      ? next.partialEditSpanBegin
      : static_cast<uint32_t>(next.seq.size());
  appendRawKeys(next, rawKeys, config_);
  const uint32_t spanEnd = static_cast<uint32_t>(next.seq.size());
  next.editSpans.push(EditSequenceSpan{spanBegin, spanEnd});
  next.hasPartialEditSpan = false;
  return afterEditCompleted(std::move(next), editIndex);
}

Outcome View::acceptSnapshot(const Lines& liveLines, CursorPos liveCursor,
                             string_view rawKeys, bool insertMode) {
  if (std::holds_alternative<Insert>(state_.phase)) {
    if (insertMode)
      return {};

    if (liveLines == state_.lines) {
      return cancelInsert();
    }

    auto outcome = acceptInsertExit(liveLines, liveCursor, rawKeys);
    if (!outcome) {
      Rejected rejected = std::move(outcome.error());
      cancelInsert();
      return unexpected(std::move(rejected));
    }
    return outcome;
  }

  const int editIndex = phaseIndex(state_.phase);
  const bool hasPlannedEdit = editIndex < totalEdits_;

  if (hasPlannedEdit) {
    const auto plannedEdit = plan_.plannedEditAt(editIndex);
    auto residualInsertion = insertionResidualBetween(
        liveLines, plannedEdit.postFencepost);

    if (insertMode && liveLines == state_.lines && residualInsertion &&
        liveCursor == residualInsertion->beginPos &&
        containsInsertCursor(liveLines, liveCursor)) {
      State next = state_;
      next.cursor = liveCursor;
      next.phase = Insert{editIndex};
      return commit(std::move(next));
    }

    if (liveLines != plannedEdit.postFencepost &&
        residualInsertion) {
      const bool validInsertCursor =
          liveCursor == residualInsertion->beginPos &&
          containsInsertCursor(liveLines, liveCursor);
      const bool validDeleteCursor =
          std::holds_alternative<Transform>(state_.phase) &&
          plannedEdit.diff.isReplacement() &&
          liveLines.contains(liveCursor);
      if ((insertMode && validInsertCursor) ||
          (!insertMode && validDeleteCursor)) {
        State next = state_;
        next.lines = liveLines;
        next.cursor = liveCursor;
        if (insertMode) {
          next.phase = Insert{editIndex};
        } else {
          if (!next.hasPartialEditSpan) {
            next.hasPartialEditSpan = true;
            next.partialEditSpanBegin = static_cast<uint32_t>(next.seq.size());
          }
          appendRawKeys(next, rawKeys, config_);
          next.phase = Transform{editIndex};
        }
        return commit(std::move(next));
      }
    }
  }

  if (liveLines == state_.lines) {
    if (insertMode) {
      if (!containsInsertCursor(liveLines, liveCursor)) {
        return unexpected(
            Rejected{"snapshot reported an invalid insert cursor position"});
      }
      State next = state_;
      next.cursor = liveCursor;
      next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
      if (hasPlannedEdit && std::holds_alternative<Transform>(next.phase)) {
        next.phase = Insert{editIndex};
        return commit(std::move(next));
      }
      return unexpected(
          Rejected{"insert-mode entry outside planned edit range"});
    }

    if (!liveLines.contains(liveCursor)) {
      return unexpected(
          Rejected{"snapshot reported an invalid cursor position"});
    }
    if (liveCursor == state_.cursor && rawKeys.empty())
      return {};

    State next = state_;
    next.cursor = liveCursor;
    appendRawKeys(next, rawKeys, config_);
    next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
    return commit(std::move(next));
  }

  return acceptBufferState(liveLines, liveCursor, rawKeys);
}

Outcome View::cancelInsert() {
  auto gated = requireInsert("insert cancel");
  if (!gated)
    return unexpected(std::move(gated.error()));

  if (state_.hasPartialEditSpan) {
    state_.phase = Transform{*gated};
    return {};
  }

  assert(!undo_.empty() && "Insert without a beginInsert undo snapshot");
  state_ = undo_.back();
  undo_.pop_back();
  return {};
}

Outcome View::undo() {
  if (undo_.empty())
    return unexpected(Rejected{"nothing to undo"});
  if (isHistoryCheckpoint(state_)) {
    redo_.push_back(state_);
  }
  state_ = undo_.back();
  undo_.pop_back();
  return {};
}

Outcome View::redo() {
  if (redo_.empty())
    return unexpected(Rejected{"nothing to redo"});
  if (isHistoryCheckpoint(state_)) {
    undo_.push_back(state_);
  }
  state_ = redo_.back();
  redo_.pop_back();
  return {};
}

// =============================================================================
// Debug
// =============================================================================

// phaseKindName overloads live in Explore.h alongside Phase.

} // namespace Explore
