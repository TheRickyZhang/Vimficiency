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
  Lines insertLines = Lines::unflatten(diff.insertedText);

  if (diff.isPureInsertion()) {
    const int fnb = VimCore::firstNonBlankColInLineStr(lines[line]);
    const int lineLen = static_cast<int>(lines[line].size());
    if (col == fnb)
      return buildTypedCommands(insertLines, "", lines[line].substr(0, fnb));
    if (col == lineLen)
      return buildTypedCommands(insertLines, "", lines[line]);
    return buildTypedCommands(insertLines, "", lines[line].substr(0, col));
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
  Lines inserted = diff.insertedLines();
  if (inserted.empty()) return diff.beginPos;
  if (inserted.size() == 1) {
    int lastCol = diff.beginPos.col +
        std::max(0, static_cast<int>(inserted[0].size()) - 1);
    return CursorPos(diff.beginPos.line, lastCol);
  }
  int lastLine = diff.beginPos.line + static_cast<int>(inserted.size()) - 1;
  int lastCol = inserted.back().empty()
      ? 0
      : static_cast<int>(inserted.back().size()) - 1;
  return CursorPos(lastLine, lastCol);
}

bool containsInsertCursor(const Lines& lines, CursorPos pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size()))
    return false;
  return pos.col >= 0 && pos.col <= static_cast<int>(lines[pos.line].size());
}

std::optional<DiffState> singleDiffBetween(
    const Lines& from,
    const Lines& to) {
  // Preserve physical matching tails so delete-first options stay literal.
  const std::string fromText = from.flatten();
  const std::string toText = to.flatten();
  if (fromText == toText) return std::nullopt;

  int prefixLen = 0;
  const int maxPrefix = static_cast<int>(std::min(fromText.size(), toText.size()));
  while (prefixLen < maxPrefix && fromText[prefixLen] == toText[prefixLen])
    prefixLen++;

  int suffixLen = 0;
  const int maxSuffix =
      static_cast<int>(std::min(fromText.size(), toText.size())) - prefixLen;
  while (suffixLen < maxSuffix &&
         fromText[static_cast<int>(fromText.size()) - 1 - suffixLen] ==
             toText[static_cast<int>(toText.size()) - 1 - suffixLen]) {
    suffixLen++;
  }

  auto flatIndexToPosition = [](std::string_view text, int idx) {
    int line = 0;
    int col = 0;
    for (int i = 0; i < idx && i < static_cast<int>(text.size()); i++) {
      if (text[i] == '\n') {
        line++;
        col = 0;
      } else {
        col++;
      }
    }
    return CursorPos(line, col);
  };

  auto advanceByText = [](CursorPos pos, std::string_view text) {
    for (char c : text) {
      if (c == '\n') {
        pos.line++;
        pos.setCol(0);
      } else {
        pos.setCol(pos.col + 1);
      }
    }
    return pos;
  };

  const int deletedLen =
      static_cast<int>(fromText.size()) - prefixLen - suffixLen;
  const int insertedLen =
      static_cast<int>(toText.size()) - prefixLen - suffixLen;
  std::string deleted = fromText.substr(prefixLen, deletedLen);
  std::string inserted = toText.substr(prefixLen, insertedLen);
  CursorPos begin = flatIndexToPosition(fromText, prefixLen);
  CursorPos end = advanceByText(begin, deleted);
  return DiffState(
      begin, end, std::move(deleted), std::move(inserted),
      TransformBoundary(from, begin, end));
}

std::optional<DiffState> insertionResidualBetween(
    const Lines& from,
    const Lines& to) {
  auto diff = singleDiffBetween(from, to);
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
  //
  // Explore opts into the traced variant because it needs plan-aligned edit
  // spans to render the Optimal-N header columns. `vf_analyze` and any other
  // non-display caller must use the plain `optimize()` so they pay zero
  // per-state tracing cost.
  CompositionOptimizer opt(config_);
  CompositionTraceResult traced = opt.optimizeWithEditSpans(
      state_.lines, initialPos, goalLines_, goalPos_,
      compositionParams_, userSequence, boundary_, navContext_);
  plan_ = std::move(traced.result);
  optimalEditSpans_ = std::move(traced.editSpansByResult);

  totalEdits_ = plan_.totalEdits();
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

// =============================================================================
// Internal helpers
// =============================================================================

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
  auto currentResidual = singleDiffBetween(state_.lines, plannedEdit.postFencepost);

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
  next.cursor = eff->postCursor;
  // Capture the edit's byte span BEFORE appending so begin = pre-append size,
  // end = post-append size. Plan-aligned with composition output.
  const uint32_t spanBegin = static_cast<uint32_t>(next.seq.size());
  next.seq.append(text);
  const uint32_t spanEnd = static_cast<uint32_t>(next.seq.size());
  next.editSpans.push(EditSequenceSpan{spanBegin, spanEnd});
  next.hasPartialEditSpan = false;
  next.cost = getEffort(next.seq, config_);
  return afterEditCompleted(std::move(next), editIndex);
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
