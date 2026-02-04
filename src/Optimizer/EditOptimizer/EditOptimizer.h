#pragma once

#include <vector>

#include "Optimizer/Config.h"
#include "Optimizer/Result.h"
#include "EditOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/EditBoundary.h"

#include "Utils/Lines.h"


// TODO: Is it helpful to return some sort of vector<Position> to reuse information about how flat indices -> real positions?
struct EditResult {
  // Results indexed by flattened starting position
  // typeAllResults[0] contains the best result for position 0 (may be replacement or delete+type)
  std::vector<Result> typeAllResults;

  // Search statistics for debugging and benchmarking
  SearchStats stats;

  // Precomputed for O(1) flat index lookup from buffer positions
  // lineBaseIndex[i] = sum of effective sizes of lines 0..i-1, minus column offset
  // Column offset is firstCol for line 0, else 0
  //
  // Usage: flatIndex = lineBaseIndex[bufferLine - firstLine] + bufferCol
  //
  // Example for edit region {"hello", "world"} starting at buffer pos (5, 3):
  //   lineBaseIndex[0] = 0 - 3 = -3      (first line, subtract firstCol)
  //   lineBaseIndex[1] = 5 - 0 = 5       (second line, no offset)
  //
  // Buffer pos (5, 7) -> flatIndex = lineBaseIndex[0] + 7 = -3 + 7 = 4  (4th char of "hello")
  // Buffer pos (6, 2) -> flatIndex = lineBaseIndex[1] + 2 = 5 + 2 = 7   (7th char overall)
  int firstLine = 0;
  int firstCol = 0;
  std::vector<int> lineBaseIndex;

  // Cursor position after edit completes (in buffer coordinates)
  // This is where the cursor lands after the change command + typed text + <Esc>
  Position goalPos;

  explicit EditResult(int n) {
    typeAllResults.resize(n);
  }

  // Compute lineBaseIndex for O(1) buffer position to flat index conversion
  // Must be called after construction with the initial lines and buffer coordinates
  void computeLineBaseIndex(const Lines& initialLines, int bufferFirstLine, int bufferFirstCol) {
    firstLine = bufferFirstLine;
    firstCol = bufferFirstCol;
    lineBaseIndex.clear();
    lineBaseIndex.reserve(initialLines.size());

    int cumSum = 0;
    for (size_t i = 0; i < initialLines.size(); i++) {
      int colOffset = (i == 0) ? firstCol : 0;
      lineBaseIndex.push_back(cumSum - colOffset);
      cumSum += static_cast<int>(initialLines[i].size());
    }
  }

  // Convert buffer position to flat index within edit region
  // Returns -1 if position is not in the edit region
  int flatIndexAt(int bufferLine, int bufferCol) const {
    int editLine = bufferLine - firstLine;
    if (editLine < 0 || editLine >= static_cast<int>(lineBaseIndex.size())) {
      return -1;
    }
    return lineBaseIndex[editLine] + bufferCol;
  }

  int flatIndexAt(const Position& bufferPos) const {
    return flatIndexAt(bufferPos.line, bufferPos.col);
  }
};

std::ostream& operator<<(std::ostream& os, const EditResult& editResult);

// Try to find an optimal replacement sequence for same-length transformations.
// Returns the result for starting position 0 (the only one ever consumed).
//
// @param deleted   The characters being removed (no newlines)
// @param inserted  The characters being added (must be same length as deleted)
// @param config    Keyboard config for cost calculation
// @return Result for position 0, or nullopt if replacement not applicable
std::optional<Result> tryReplacement(const std::string& deleted,
                                     const std::string& inserted,
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
      EditOptimizerParams params = {}
  );

  // find optimal sequences to delete all content in initialLines
  // Simpler than optimizeEdit: no typed content, no change conversion
  // Returns EditResult with typeAllResults indexed by flattened starting position
  EditResult optimizePureDeletion(
      const Lines& initialLines,
      EditBoundary editBoundary,
      EditOptimizerParams params = {}
  );
};
