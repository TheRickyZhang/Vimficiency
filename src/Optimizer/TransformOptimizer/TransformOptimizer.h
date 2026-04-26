#pragma once

#include <span>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/OptimizerResult.h"
#include "Optimizer/Result.h"
#include "TransformOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Boundary/TransformBoundary.h"

#include "Types/Lines.h"


struct TransformResult : BaseOptimizerResult<std::vector<Result>> {
  TransformResult() = default;

  // Constructor: initializes results and flat index for buffer-position lookup.
  // Buffer-position params default to edit-region-local (line 0, col 0).
  // results is a 2D vector: one bucket of Results per starting position.
  TransformResult(std::vector<std::vector<Result>> results, TransformSearchStats stats,
             const Lines& initialLines, int bufferBeginLine = 0,
             int bufferBeginCol = 0, CursorPos goalPos = {0, 0});

  const TransformSearchStats& getStats() const { return stats_; }

  // Look up the best result for a buffer position. Returns nullptr if the position
  // is outside the transform region or has no result.
  // Return a nullable, const & view.
  // TODO (C++ 26): use optional<const T&>
  const Result* resultAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return nullptr;
    const auto& bucket = results_[static_cast<size_t>(idx)];
    return bucket.empty() ? nullptr : &bucket[0];
  }

  // All captured results for this starting position, sorted by cost (best first).
  std::span<const Result> resultsAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return {};
    if (idx >= static_cast<int>(results_.size())) return {};
    const auto& bucket = results_[static_cast<size_t>(idx)];
    return {bucket.data(), bucket.size()};
  }

  const CursorPos& getGoalPos() const { return goalPos_; }

  // Returns goal position for a given starting position.
  // Pure deletions: per-start goal from goalPosByStart_ (with fallback to goalPos_).
  // Regular edits: shared goalPos_.
  CursorPos goalPosAt(int bufferLine, int bufferCol) const {
    if (!goalPosByStart_.empty()) {
      int idx = resultIndexAt(bufferLine, bufferCol);
      if (idx >= 0 && idx < static_cast<int>(goalPosByStart_.size()))
        return goalPosByStart_[static_cast<size_t>(idx)];
    }
    return goalPos_;
  }

  bool hasPerStartGoals() const { return !goalPosByStart_.empty(); }

  void setGoalPosByStart(std::vector<CursorPos> goals) {
    goalPosByStart_ = std::move(goals);
  }

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
  std::vector<CursorPos> goalPosByStart_;

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
