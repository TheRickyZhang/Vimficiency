#include "Explore.h"

#include <cassert>
#include <utility>
#include <variant>

#include "EditHandler.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "MovementHandler.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/NavOptimizer/NavFrontier.h"
#include "Optimizer/TransformOptimizer/TransformFrontier.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimOptions.h"

using namespace std;

namespace Explore {

namespace {

// Shared rawKeys handling for accept*/apply* actions that fold reported
// keys into seq. parseSequence accepts edits (unlike parseMotions),
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
  next.seq.append(rawKeys);
  next.cost = getEffort(next.seq, config);
  return {};
}

// Derive the typed-text portion (no trailing <Esc>) for the canonical
// insert-mode strategy implied by `diff`. Returns empty string if the diff
// has no insert-mode component (pure deletion).
//
// MIRROR: matches the pure-insertion strategy construction in
// `TransformFrontier.cpp::rankTransformFrontier`. Replacements use the
// canonical inserted text with prefix-aware autoindent; the chosen
// structural command (s/cl/cw/...) is irrelevant to the typed tail
// because all consistent structural commands leave the cursor at
// `diff.beginPos` ready to receive the inserted content.
KeyedSequence buildInsertModeTypedSequence(const DiffState& diff,
                                            const Lines& lines) {
  if (diff.isPureInsertion() && diff.isNewLineInsertion() &&
      diff.beginPos.col == 0 && diff.beginPos.line > 0) {
    // o-style new-line insertion: source indent comes from the previous line.
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

  // Replacement: structural command leaves cursor at diff.beginPos with
  // everything before that column unchanged.
  return buildTypedCommands(insertLines, "", lines[line].substr(0, col));
}

// Returns the typed text (no trailing <Esc>) for a diff with insert-mode
// content. Empty string for pure deletions.
std::string deriveInsertModeTypedText(const DiffState& diff,
                                       const Lines& lines) {
  if (diff.isPureDeletion()) return "";

  KeyedSequence ks = buildInsertModeTypedSequence(diff, lines);

  // Strip trailing <Esc> token — Lua handles Esc via InsertLeave, so the
  // recommendation token represents only what the user types in insert mode.
  std::string_view raw = ks.seq.view();
  static constexpr std::string_view ESC_STR = "<Esc>";
  if (raw.size() >= ESC_STR.size() &&
      raw.substr(raw.size() - ESC_STR.size()) == ESC_STR) {
    raw.remove_suffix(ESC_STR.size());
  }
  return std::string(raw);
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

pair<CursorPos, CursorPos> View::currentTargetRange() const {
  CharRange r = plan_.getPlan().navTarget(phaseIndex(state_.phase));
  return {r.begin, r.end};
}

bool View::isCompleted() const {
  auto* nav = std::get_if<Navigate>(&state_.phase);
  return nav && nav->index == totalEdits_ && state_.cursor == goalPos_;
}

vector<Suggestion> View::recommendations(int maxCount,
                      const OptimizerParamOverrides* overrides) const {
  return std::visit(overload{
      [&](const Navigate& nav) {
        return recommendNavigate(nav.index, maxCount, overrides);
      },
      [&](const Transform& transform) {
        return recommendTransform(transform.index, maxCount, overrides);
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
  undo_.push_back(state_);
  redo_.clear();
  state_ = std::move(next);
  return {};
}

vector<Suggestion> View::recommendNavigate(
    int navIndex,
    int maxCount,
    const OptimizerParamOverrides* overrides) const {
  CharRange target = plan_.getPlan().navTarget(navIndex);
  return rankNavFrontier(
      NavFrontierQuery{
          FrontierQuery{
              .lines = state_.lines,
              .cursor = state_.cursor,
              .seq = state_.seq,
              .maxCount = maxCount,
              .overrides = overrides,
          },
          target,
          boundary_,
          navContext_,
      },
      config_);
}

vector<Suggestion> View::recommendTransform(
    int editIndex,
    int maxCount,
    const OptimizerParamOverrides* overrides) const {
  assert(editIndex >= 0 && editIndex < totalEdits_ && "Transform editIndex out of range");

  const DiffState& diff = plan_.plannedEditAt(editIndex).diff;
  return rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = state_.lines,
              .cursor = state_.cursor,
              .seq = state_.seq,
              .maxCount = maxCount,
              .overrides = overrides,
          },
          diff,
      },
      config_);
}

vector<Suggestion> View::recommendInsert(int editIndex, int maxCount) const {
  if (maxCount <= 0) return {};
  assert(editIndex >= 0 && editIndex < totalEdits_ &&
         "Insert editIndex out of range");

  // state_.lines is the pre-edit fencepost during Insert (afterEditCompleted
  // hasn't run yet), so it shares coordinate space with `diff.beginPos`.
  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  std::string typedText =
      deriveInsertModeTypedText(plannedEdit.diff, state_.lines);
  if (typedText.empty()) return {};

  // costDiff: same monoid composition the frontier modules use, so the
  // displayed cost reflects the actual incremental effort of typing this
  // sequence after `state_.seq`'s last keystroke. (Boundary
  // interactions like same-finger / roll across the structural→typed seam
  // are already accumulated into seq once `s`/`cl`/`cw` was
  // committed.)
  RunningEffort acceptedEffort(
      globalSequenceToKeys().tokenize(state_.seq), config_);
  const double acceptedCost = acceptedEffort.getEffort(config_);
  RunningEffort candidate(globalSequenceToKeys().tokenize(typedText), config_);
  RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate);
  const double costDiff = merged.getEffort(config_) - acceptedCost;

  return {Suggestion{
      .token = Token{typedText},
      .landingPos = plannedEdit.transformResult.getGoalPos(),
      .costDiff = costDiff,
  }};
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
  // Always transition to phaseForEditCursor(nextEdit, ...). For
  // nextEdit == totalEdits_ that returns Navigate{totalEdits_} — the
  // post-final-edit nav segment. Completion is the derived predicate
  // `isCompleted()` (Navigate at totalEdits_ with cursor at goalPos).
  next.lines = plan_.getPlan().fencepostAt(nextEdit);
  next.phase = phaseForEditCursor(nextEdit, next.cursor);
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
  if (!movementStaysInTransformRange(next.cursor)) {
    return unexpected(
        Rejected{"transform movement left the current edit range"});
  }
  next.seq.append(std::move(eff->appendedSeq));
  next.cost = getEffort(next.seq, config_);
  next.phase = phaseForEditCursor(*gated, next.cursor);
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
    next.seq.append(std::move(eff->appendedSeq));
    next.cost = getEffort(next.seq, config_);
  }
  next.phase = phaseForEditCursor(*gated, next.cursor);
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
  next.seq.append(text);
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
  if (auto e = appendRawKeysOrReject(next, rawKeys, config_); !e) {
    return unexpected(std::move(e.error()));
  }
  if (eff->advance) {
    return afterEditCompleted(std::move(next), editIndex);
  }
  // No-op: buffer already matches current fencepost (e.g. native undo or
  // programmatic re-sync). Sync cursor only.
  next.phase = phaseForEditCursor(editIndex, next.cursor);
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
  // uniform fencepost advancement path.
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
  return {};
}

Outcome View::undo() {
  if (undo_.empty())
    return unexpected(Rejected{"nothing to undo"});
  redo_.push_back(state_);
  state_ = undo_.back();
  undo_.pop_back();
  return {};
}

Outcome View::redo() {
  if (redo_.empty())
    return unexpected(Rejected{"nothing to redo"});
  undo_.push_back(state_);
  state_ = redo_.back();
  redo_.pop_back();
  return {};
}

// =============================================================================
// Debug
// =============================================================================

// phaseKindName overloads live in Explore.h alongside Phase.

} // namespace Explore
