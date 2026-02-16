#pragma once

#include <vector>

#include "Optimizer/Config.h"
#include "Optimizer/Result.h"
#include "EditOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/EditBoundary.h"

#include "Utils/Lines.h"


struct EditResult {
  // Cursor position after edit completes (in buffer coordinates)
  // This is where the cursor lands after the change command + typed text + <Esc>
  Position goalPos;

  // Search statistics for debugging and benchmarking
  SearchStats stats;

  // Constructor: initializes results and flat index for buffer-position lookup.
  // Buffer-position params default to edit-region-local (line 0, col 0).
  EditResult(std::vector<Result> results, SearchStats stats,
             const Lines& initialLines, int bufferFirstLine = 0,
             int bufferFirstCol = 0, Position goalPos = {0, 0});

  // Look up the result for a buffer position. Returns nullptr if the position
  // is outside the edit region or the result at that position is invalid.
  // Return a nullable, const & view.
  // TODO (C++ 26): use optional<const T&>
  const Result* resultAt(int bufferLine, int bufferCol) const {
    int editLine = bufferLine - firstLine_;
    if (editLine < 0 || editLine >= static_cast<int>(lineBaseIndex_.size()))
      return nullptr;
    int idx = lineBaseIndex_[editLine] + bufferCol;
    if (idx < 0 || idx >= static_cast<int>(results_.size()))
      return nullptr;
    const Result& r = results_[idx];
    return r.isValid() ? &r : nullptr;
  }

  // Read-only access to the full results vector (for iteration, suffix cost computation, tests)
  const std::vector<Result>& getResults() const { return results_; }

  // Number of result entries
  size_t resultCount() const { return results_.size(); }

private:
  // Results indexed by flattened starting position
  std::vector<Result> results_;

  // Precomputed for O(1) flat index lookup from buffer positions
  // lineBaseIndex[i] = sum of effective sizes of lines 0..i-1, minus column offset
  // Column offset is firstCol for line 0, else 0
  //
  // Usage: flatIndex = lineBaseIndex[bufferLine - firstLine] + bufferCol
  int firstLine_ = 0;
  int firstCol_ = 0;
  std::vector<int> lineBaseIndex_;

  friend std::ostream& operator<<(std::ostream& os, const EditResult& editResult);
};

std::ostream& operator<<(std::ostream& os, const EditResult& editResult);


struct EditOptimizer {
  Config config;

  EditOptimizer(const Config& config) : config(std::move(config)) {}

  // find optimal sequences to transform initialLines to goalLines
  // Uses suffix caching for cross-position sharing: when one starting position
  // finds a path through an intermediate state, the remaining commands are
  // cached so other positions reaching the same state get an instant result.
  // Returns results indexed by flattened starting position
  EditResult optimizeEdit(
      const Lines& initialLines,
      const Lines& goalLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {},
      int bufferFirstLine = 0,
      int bufferFirstCol = 0,
      Position goalPos = {0, 0}
  );


private:
  // Unified implementation: PureDeletion=true for deletion-only, false for full edit
  template<bool PureDeletion>
  EditResult optimizeImpl(
      const Lines& initialLines,
      const Lines& goalLines,
      EditBoundary editBoundary,
      EditOptimizerParams params,
      int bufferFirstLine,
      int bufferFirstCol,
      Position goalPos
  );
};
