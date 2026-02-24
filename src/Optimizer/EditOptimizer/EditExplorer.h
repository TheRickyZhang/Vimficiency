#pragma once

#include <functional>

#include "EditToSpec.h"
#include "Types/LineRange.h"
#include "Types/CursorPos.h"
#include "Types/Range.h"
#include "Keyboard/KeyedSequence.h"
#include "Optimizer/EditOptimizer/EditState.h"
#include "Optimizer/SequenceBinding.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "Types/EdgeType.h"

// Forward declarations
struct EditSearchContext;
struct EditBoundary;

// Forward declare callback types (defined in EditSearchContext.h)
// Each callback receives a fully bound command payload.
using DeletionCallback = std::function<void(const Range&, const SequenceBinding&)>;
using LinewiseCallback = std::function<void(int line, const SequenceBinding&)>;
using JoinCallback = std::function<void(bool addSpace, const SequenceBinding&)>;

// Counted operation callbacks
using CountedLinewiseCallback = std::function<void(LineRange, const SequenceBinding&)>;
using CountedJoinCallback = std::function<void(bool addSpace, const SequenceBinding&)>;

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
  // Caller must check boundary region before calling (via exploreBoundaryEscape).
  void exploreAllDeletions(const EditState& state,
                           DeletionCallback onDeletion,
                           LinewiseCallback onLinewise = nullptr,
                           JoinCallback onJoin = nullptr);

  // Explore J/gJ commands
  void exploreJoinCommands(const CursorPos& cursor, const Lines& lines, JoinCallback onJoin);

  // Explore counted line edits: dj, dk, {n}dd
  void exploreCountedLineEdits(const CursorPos& cursor, const Lines& lines,
                               int minCountRepeat,
                               CountedLinewiseCallback onCountedLinewise);

  // Explore counted join commands: {n}J, {n}gJ
  void exploreCountedJoinCommands(const CursorPos& cursor, const Lines& lines,
                                  int minCountRepeat,
                                  CountedJoinCallback onCountedJoin);

  // Explore counted word edits: {n}de, {n}dE, {n}dw, {n}dW, {n}db, {n}dB, {n}dge, {n}dgE
  void exploreCountedWordEdits(const CursorPos& cursor, const Lines& lines,
                               int minCountRepeat,
                               DeletionCallback onDeletion);

  // Explore counted char edits: {n}x
  void exploreCountedCharEdits(const CursorPos& cursor, const Lines& lines,
                               int contentStart, int contentEnd,
                               int minCountRepeat,
                               DeletionCallback onDeletion);

  // ================== Templated Exploration Methods ==================
  // EdgeType known at compile time for branch elimination

  template<EdgeType Edge>
  void exploreForwardWordEdits(
      const std::vector<Edit::ForwardWordEditSpecNoEdge>& specs,
      const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion);

  template<EdgeType Edge>
  void exploreBackwardWordEdits(
      const std::vector<Edit::BackwardWordEditSpecNoEdge>& specs,
      const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion);

  template<bool Forward>
  void exploreParagraphEdits(
      const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
      const CursorPos& cursor, const Lines& lines,
      DeletionCallback onDeletion, LinewiseCallback onLinewise);

  template<bool Forward>
  void exploreSentenceEdits(
      const std::vector<Edit::SentenceEditSpecNoDir>& specs,
      const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion);

  // ================== Non-templated Exploration Methods ==================
  void exploreTextObjectEdits(
      const std::vector<Edit::TextObjectEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion);

  void exploreHalfLineEdits(
      const std::vector<Edit::LineEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines,
      int contentStart, int contentEnd, DeletionCallback onDeletion);

  void exploreFullLineEdits(
      const std::vector<Edit::FullLineEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines, LinewiseCallback onLinewise);

  void exploreCharEdits(
      const CursorPos& cursor, const Lines& lines,
      int contentStart, int contentEnd, int editContentLen,
      DeletionCallback onDeletion);

private:
  EditSearchContext& ctx_;

  // Helper methods
  bool inBoundaryRegion(const CursorPos& pos, const Lines& lines) const;
  std::pair<int, int> computeEditBounds(const Lines& lines, const CursorPos& cursor) const;
};
