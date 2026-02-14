// tests/Benchmarks/BenchUtils.h
//
// Common utilities for Google Benchmark-based benchmarks.

#pragma once

#include <string>

#include <benchmark/benchmark.h>

#include "Optimizer/SearchStats.h"
#include "Utils/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/SeedManager.h"

// =============================================================================
// Buffer Generation
// =============================================================================

enum class BufferShape { Uniform, Prose, CodeLike };

constexpr int DEFAULT_RANGE_SIZE = 6;
constexpr int DEFAULT_SEED_COUNT = 5;

inline Lines generateBuffer(int numLines = 20, int avgLineLen = 30, BufferShape shape = BufferShape::CodeLike) {
  switch (shape) {
    case BufferShape::Uniform:
      return randomProseLines(numLines, avgLineLen, avgLineLen);
    case BufferShape::Prose:
      return randomProseBuffer(numLines);
    case BufferShape::CodeLike:
      return randomCodeBuffer(numLines, avgLineLen);
  }
  return {}; // unreachable
}

// =============================================================================
// Seed Mode Description
// =============================================================================

inline std::string getSeedModeDescription() {
  auto& sm = SeedManager::instance();
  if (sm.isRandom()) {
    return "random (logged to " + sm.getSeedFilePath() + ")";
  } else if (sm.isReplay()) {
    return "replay from " + sm.getSeedFilePath();
  } else {
    return "fixed (" + std::to_string(sm.getBaseFixedSeed()) + "-" +
           std::to_string(sm.getBaseFixedSeed() + DEFAULT_SEED_COUNT - 1) + ")";
  }
}

// =============================================================================
// Google Benchmark Custom Counters
// =============================================================================

// Accumulate stats across iterations. Call once per iteration inside the loop.
inline void accumulateStats(SearchStats& accumulated, const SearchStats& iteration) {
  accumulated.nodesExplored += iteration.nodesExplored;
  accumulated.resultsFound += iteration.resultsFound;
  accumulated.queueSizeAtStop += iteration.queueSizeAtStop;
  accumulated.statesSkipped += iteration.statesSkipped;
  if (iteration.isRangeSearch()) {
    if (!accumulated.isRangeSearch()) accumulated.uniquePositionsFound = 0;
    accumulated.uniquePositionsFound += iteration.uniquePositionsFound;
  }
  accumulated.motionNodesExplored += iteration.motionNodesExplored;
  accumulated.editNodesExplored += iteration.editNodesExplored;
}

inline void setSearchCounters(benchmark::State& state, const SearchStats& stats) {
  state.counters["Searched"] = benchmark::Counter(
      stats.nodesExplored, benchmark::Counter::kAvgIterations);
  state.counters["Found"] = benchmark::Counter(
      stats.resultsFound, benchmark::Counter::kAvgIterations);
  state.counters["Remain"] = benchmark::Counter(
      stats.queueSizeAtStop, benchmark::Counter::kAvgIterations);
  if (stats.isRangeSearch()) {
    state.counters["Unique"] = benchmark::Counter(
        stats.uniquePositionsFound, benchmark::Counter::kAvgIterations);
  }
  if (stats.motionNodesExplored > 0 || stats.editNodesExplored > 0) {
    state.counters["MotionNodes"] = benchmark::Counter(
        stats.motionNodesExplored, benchmark::Counter::kAvgIterations);
    state.counters["EditNodes"] = benchmark::Counter(
        stats.editNodesExplored, benchmark::Counter::kAvgIterations);
  }
}
