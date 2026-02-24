#pragma once

#include <type_traits>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/Result.h"
#include "EditOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/EditBoundary.h"

#include "Types/Lines.h"


struct EditResult {
  // Cursor position after edit completes (in buffer coordinates)
  // This is where the cursor lands after the change command + typed text + <Esc>
  CursorPos goalPos;

  // Search statistics for debugging and benchmarking
  SearchStats stats;

  // Constructor: initializes results and flat index for buffer-position lookup.
  // Buffer-position params default to edit-region-local (line 0, col 0).
  EditResult(std::vector<Result> results, SearchStats stats,
             const Lines& initialLines, int bufferBeginLine = 0,
             int bufferBeginCol = 0, CursorPos goalPos = {0, 0});

  // Look up the result for a buffer position. Returns nullptr if the position
  // is outside the edit region or the result at that position is invalid.
  // Return a nullable, const & view.
  // TODO (C++ 26): use optional<const T&>
  const Result* resultAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return nullptr;
    const Result& r = results_[static_cast<size_t>(idx)];
    return r.isValid() ? &r : nullptr;
  }

  // Read-only access to the full results vector (for iteration, suffix cost computation, tests)
  const std::vector<Result>& getResults() const { return results_; }

  // Number of result entries
  size_t resultCount() const { return results_.size(); }

  // Flat result index for a buffer position, or -1 if out of range.
  int resultIndexAt(int bufferLine, int bufferCol) const {
    int editLine = bufferLine - beginLine_;
    if (editLine < 0 || editLine >= static_cast<int>(lineBaseIndex_.size()))
      return -1;
    int idx = lineBaseIndex_[editLine] + bufferCol;
    if (idx < 0 || idx >= static_cast<int>(results_.size()))
      return -1;
    return idx;
  }

private:
  // Results indexed by flattened starting position
  std::vector<Result> results_;

  // Precomputed for O(1) flat index lookup from buffer positions
  // lineBaseIndex[i] = sum of effective sizes of lines 0..i-1, minus column offset
  // Column offset is beginCol for line 0, else 0
  //
  // Usage: flatIndex = lineBaseIndex[bufferLine - beginLine] + bufferCol
  int beginLine_ = 0;
  int beginCol_ = 0;
  std::vector<int> lineBaseIndex_;

  friend std::ostream& operator<<(std::ostream& os, const EditResult& editResult);
};

std::ostream& operator<<(std::ostream& os, const EditResult& editResult);

struct PureDeletionEditResult {
  EditResult editResult;
  // Per-start goal cursor positions in buffer coordinates (same flat index as EditResult::getResults()).
  std::vector<CursorPos> goalPosByStart;

  friend std::ostream& operator<<(std::ostream& os, const PureDeletionEditResult& pdr) {
    os << pdr.editResult;
    return os;
  }
};


struct EditOptimizer {
  Config config;

  EditOptimizer(const Config& config) : config(std::move(config)) {}

  // find optimal sequences to transform initialLines to goalLines
  // Uses suffix caching for cross-position sharing: when one starting position
  // finds a path through an intermediate state, the remaining commands are
  // cached so other positions reaching the same state get an instant result.
  // goalLines must not be a pure-deletion goal ({} or {""}); pure deletions
  // should use optimizePureDeletion().
  // Returns results indexed by flattened starting position
  EditResult optimizeEdit(
      const Lines& initialLines,
      const Lines& goalLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {},
      int bufferBeginLine = 0,
      int bufferBeginCol = 0,
      CursorPos goalPos = {0, 0}
  );

  PureDeletionEditResult optimizePureDeletion(
      const Lines& initialLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {},
      int bufferBeginLine = 0,
      int bufferBeginCol = 0,
      CursorPos goalPos = {0, 0}
  );


private:
  template<bool PureDeletion>
  using OptimizeImplResult =
      std::conditional_t<PureDeletion,
                         PureDeletionEditResult,
                         EditResult>;

  // Unified implementation: PureDeletion=true for deletion-only, false for full edit
  template<bool PureDeletion>
  OptimizeImplResult<PureDeletion> optimizeImpl(
      const Lines& initialLines,
      const Lines& goalLines,
      EditBoundary editBoundary,
      EditOptimizerParams params,
      int bufferBeginLine,
      int bufferBeginCol,
      CursorPos goalPos
  );
};
