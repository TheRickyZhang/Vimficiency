// tests/Benchmarks/BenchUtils.h
//
// Common utilities for Google Benchmark-based benchmarks.

#pragma once

#include <string>

#include <benchmark/benchmark.h>

#include "Optimizer/SearchStats.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/SeedManager.h"

// =============================================================================
// Buffer Generation
// =============================================================================
// BufferShape, generateBuffer, and DEFAULT_RANGE_SIZE/DEFAULT_SEED_COUNT live in
// Utils/RandomBufferHelpers.h (shared with the explore tool and the
// optimizer-case catalog via test_utils).

constexpr double DEFAULT_BENCH_MIN_TIME = 0.05;

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
template<typename S>
void accumulateStats(S& accumulated, const S& iteration) {
  accumulated.accumulateFrom(iteration);
}

template<typename S>
void setSearchCounters(benchmark::State& state, const S& stats) {
  state.counters["Searched"] = benchmark::Counter(
      stats.nodesExplored(), benchmark::Counter::kAvgIterations);
  state.counters["Pops"] = benchmark::Counter(
      stats.totalPops(), benchmark::Counter::kAvgIterations);
  state.counters["Found"] = benchmark::Counter(
      stats.resultsFound(), benchmark::Counter::kAvgIterations);
  state.counters["Remain"] = benchmark::Counter(
      stats.queueSizeAtStop(), benchmark::Counter::kAvgIterations);
  if constexpr (requires { stats.uniquePositionsFound(); }) {
    if (stats.isRangeSearch()) {
      state.counters["Unique"] = benchmark::Counter(
          stats.uniquePositionsFound(), benchmark::Counter::kAvgIterations);
    }
  }
  if constexpr (requires { stats.navNodesExplored(); }) {
    if (stats.navNodesExplored() > 0 || stats.editNodesExplored() > 0) {
      state.counters["NavNodes"] = benchmark::Counter(
          stats.navNodesExplored(), benchmark::Counter::kAvgIterations);
      state.counters["EditNodes"] = benchmark::Counter(
          stats.editNodesExplored(), benchmark::Counter::kAvgIterations);
    }
  }
}
