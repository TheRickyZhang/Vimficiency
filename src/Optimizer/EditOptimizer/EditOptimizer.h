#pragma once

#include <optional>
#include <string_view>
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

// Try to find an optimal replacement sequence for same-length transformations.
// Returns the result for starting position 0 (the only one ever consumed).
//
// @param deleted   The characters being removed (no newlines)
// @param inserted  The characters being added (must be same length as deleted)
// @param config    Keyboard config for cost calculation
// @return Result for position 0, or nullopt if replacement not applicable
std::optional<Result> tryReplacement(std::string_view deleted,
                                     std::string_view inserted,
                                     const Config& config);


struct EditOptimizer {
  Config config;

  EditOptimizer(const Config& config) : config(std::move(config)) {}

  // find optimal sequences to transform initialLines to goalLines
  // Either delete all initial and type out result, or use replacement
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

  // find optimal sequences to delete all content in initialLines
  // Simpler than optimizeEdit: no typed content, no change conversion
  // Returns EditResult with typeAllResults indexed by flattened starting position
  EditResult optimizePureDeletion(
      const Lines& initialLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {},
      int bufferFirstLine = 0,
      int bufferFirstCol = 0,
      Position goalPos = {0, 0}
  );
};
