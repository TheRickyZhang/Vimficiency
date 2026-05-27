#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/OptimizerResult.h"
#include "Optimizer/Result.h"
#include "TransformOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/TransformBoundary.h"

#include "Types/CharRange.h"
#include "Types/Lines.h"


struct TransformResult : BaseOptimizerResult<std::vector<Result>> {
  TransformResult() = default;

  // Constructor: initializes results and flat index for buffer-position lookup.
  // Buffer-position params default to edit-region-local (line 0, col 0).
  // results is a 2D vector: one bucket of Results per starting position.
  TransformResult(std::vector<std::vector<Result>> results, TransformSearchStats stats,
             const Lines& initialLines, int bufferBeginLine = 0,
             int bufferBeginCol = 0, CursorPos goalPos = {0, 0},
             std::vector<std::vector<CursorPos>> resultGoalPositions = {});

  const TransformSearchStats& getStats() const { return stats_; }

  // Look up the best result for a buffer position. Returns nullptr if the position
  // is outside the transform region or has no result.
  // Return a nullable, const & view.
  // TODO (C++ 26): use optional<const T&>
  const Result* resultAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return nullptr;
    if (!isValidStartIndex(idx)) return nullptr;
    const auto& bucket = results_[static_cast<size_t>(idx)];
    return bucket.empty() ? nullptr : &bucket[0];
  }

  // All captured results for this starting position, sorted by cost (best first).
  std::span<const Result> resultsAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return {};
    if (idx >= static_cast<int>(results_.size())) return {};
    if (!isValidStartIndex(idx)) return {};
    const auto& bucket = results_[static_cast<size_t>(idx)];
    return {bucket.data(), bucket.size()};
  }

  const std::vector<CursorPos>& startPositions() const { return startPositions_; }

  const CursorPos& getGoalPos() const { return goalPos_; }

  // Returns the post-edit cursor for a result alternative at this start.
  CursorPos goalPosAt(
      int bufferLine, int bufferCol, std::size_t resultIndex = 0) const {
    if (!resultGoalPositions_.empty()) {
      int idx = resultIndexAt(bufferLine, bufferCol);
      if (idx >= 0 && idx < static_cast<int>(resultGoalPositions_.size()) &&
          isValidStartIndex(idx)) {
        const auto& bucket = resultGoalPositions_[static_cast<size_t>(idx)];
        if (resultIndex < bucket.size()) return bucket[resultIndex];
      }
    }
    return goalPos_;
  }

  bool hasResultGoals() const { return !resultGoalPositions_.empty(); }
  bool isValidStartPosition(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    return idx >= 0 && isValidStartIndex(idx);
  }

  void restrictValidStartRegion(CharRange validStartRegion);

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
  TransformSearchStats stats_;
  CursorPos goalPos_;
  int beginLine_ = 0;
  int beginCol_ = 0;
  std::vector<int> lineBaseIndex_;
  std::vector<CursorPos> indexToBufferPos_;
  std::vector<char> validStartByIndex_;
  std::vector<std::vector<CursorPos>> resultGoalPositions_;
  std::vector<CursorPos> startPositions_;

  bool isValidStartIndex(int idx) const {
    return validStartByIndex_.empty() ||
           validStartByIndex_[static_cast<size_t>(idx)];
  }
  void rebuildStartPositions();

  friend std::ostream& operator<<(std::ostream& os, const TransformResult& transformResult);
};

std::ostream& operator<<(std::ostream& os, const TransformResult& transformResult);


struct TransformOptimizer {
  Config config;

  TransformOptimizer(const Config& config) : config(std::move(config)) {}

  // Historical name retained: this is the transform-layer optimizer.
  // Finds optimal sequences to transform initialLines to goalLines.
  // Uses suffix caching for cross-position sharing: when one starting position
  // finds a path through an intermediate state, the remaining commands are
  // cached so other positions reaching the same state get an instant result.
  // goalLines must not be a pure-deletion goal ({} or {""}); pure deletions
  // should use optimizePureDeletion().
  // Returns results indexed by flattened starting position
  TransformResult optimizeTransform(
      const Lines& initialLines,
      const Lines& goalLines,
      TransformBoundary transformBoundary,
      TransformOptimizerParams params = {},
      int bufferBeginLine = 0,
      int bufferBeginCol = 0,
      CursorPos goalPos = {0, 0}
  );

  TransformResult optimizePureDeletion(
      const Lines& initialLines,
      TransformBoundary transformBoundary,
      TransformOptimizerParams params = {},
      int bufferBeginLine = 0,
      int bufferBeginCol = 0,
      CursorPos goalPos = {0, 0}
  );


private:
  // Unified implementation: PureDeletion=true for deletion-only, false for full edit
  template<bool PureDeletion>
  TransformResult optimizeImpl(
      const Lines& initialLines,
      const Lines& goalLines,
      TransformBoundary transformBoundary,
      TransformOptimizerParams params,
      int bufferBeginLine,
      int bufferBeginCol,
      CursorPos goalPos
  );
};
