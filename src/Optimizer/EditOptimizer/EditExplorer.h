#pragma once

#include <functional>

#include "EditToSpec.h"
#include "Editor/LineRange.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Keyboard/KeyedSequence.h"
#include "State/EditState.h"
#include "Utils/Lines.h"
#include "VimCore/EdgeType.h"

// Forward declarations
struct EditSearchContext;
struct EditBoundary;

// Forward declare callback types (defined in EditSearchContext.h)
using DeletionCallback = std::function<void(const Range&, const KeyedSequence&)>;
using LinewiseCallback = std::function<void(int, const KeyedSequence&)>;
using MotionCallback = std::function<void(const Position&, const KeyedSequence&)>;
using JoinCallback = std::function<void(bool, const KeyedSequence&)>;

// Counted operation callbacks
using CountedLinewiseCallback = std::function<void(LineRange, const KeyedSequence&)>;
using CountedJoinCallback = std::function<void(int count, bool addSpace, const KeyedSequence&)>;

// EditExplorer handles exploration of edit operations from a given state.
// Separated from EditSearchContext for cleaner organization and future extensibility.
//
// Design notes:
// - Takes a reference to EditSearchContext to access boundary info and column offsets
// - Uses callback-based emission pattern (like the original exploreAllDeletions)
// - Exploration methods are templated where EdgeType or direction is known at compile time
class EditExplorer {
public:
  explicit EditExplorer(EditSearchContext& ctx);

  // Main entry point: explore all valid deletions from current state
  // Calls callbacks for each valid operation found
  void exploreAllDeletions(const EditState& state,
                           DeletionCallback onDeletion,
                           LinewiseCallback onLinewise = nullptr,
                           MotionCallback onMotion = nullptr,
                           JoinCallback onJoin = nullptr);

  // Explore J/gJ commands
  void exploreJoinCommands(const Position& cursor, const Lines& lines, JoinCallback onJoin);

  // Explore counted line edits: dj, dk, {n}dd
  void exploreCountedLineEdits(const Position& cursor, const Lines& lines,
                               int minCountRepeat, CountedLinewiseCallback onCountedLinewise);

  // Explore counted join commands: {n}J, {n}gJ
  void exploreCountedJoinCommands(const Position& cursor, const Lines& lines,
                                  int minCountRepeat, CountedJoinCallback onCountedJoin);

  // Explore counted word edits: {n}de, {n}dE, {n}dw, {n}dW, {n}db, {n}dB, {n}dge, {n}dgE
  void exploreCountedWordEdits(const Position& cursor, const Lines& lines,
                               int minCountRepeat, DeletionCallback onDeletion);

  // ================== Templated Exploration Methods ==================
  // EdgeType known at compile time for branch elimination

  template<EdgeType Edge>
  void exploreForwardWordEdits(
      const std::vector<Edit::ForwardWordEditSpecNoEdge>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);

  template<EdgeType Edge>
  void exploreBackwardWordEdits(
      const std::vector<Edit::BackwardWordEditSpecNoEdge>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);

  template<bool Forward>
  void exploreParagraphEdits(
      const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
      const Position& cursor, const Lines& lines,
      DeletionCallback onDeletion, LinewiseCallback onLinewise);

  template<bool Forward>
  void exploreSentenceEdits(
      const std::vector<Edit::SentenceEditSpecNoDir>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);

  // ================== Non-templated Exploration Methods ==================
  void exploreTextObjectEdits(
      const std::vector<Edit::TextObjectEditSpec>& specs,
      const Position& cursor, const Lines& lines, DeletionCallback onDeletion);

  void exploreHalfLineEdits(
      const std::vector<Edit::LineEditSpec>& specs,
      const Position& cursor, const Lines& lines,
      int contentStart, int contentEnd, DeletionCallback onDeletion);

  void exploreFullLineEdits(
      const std::vector<Edit::FullLineEditSpec>& specs,
      const Position& cursor, const Lines& lines, LinewiseCallback onLinewise);

  void exploreCharEdits(
      const Position& cursor, const Lines& lines,
      int contentStart, int contentEnd, int editContentLen,
      DeletionCallback onDeletion);

private:
  EditSearchContext& ctx_;

  // Helper methods
  bool inBoundaryRegion(const Position& pos, const Lines& lines) const;
  std::pair<int, int> computeEditBounds(const Lines& lines, const Position& cursor) const;
};
