// tests/Benchmarks/BenchUtils.h
//
// Common utilities for benchmark tests.

#pragma once

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "Optimizer/SearchStats.h"
#include "Utils/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

// =============================================================================
// Timing Infrastructure
// =============================================================================

struct TimingStats {
  double avgMs;
  double minMs;
  double maxMs;
  double medianMs;
  int iterations;
};

template <typename Func>
TimingStats measureTiming(Func&& fn, int iterations, int warmupRuns = 2) {
  // Warmup runs
  for (int i = 0; i < warmupRuns; i++) {
    fn();
  }

  // Timed runs
  std::vector<double> times;
  times.reserve(iterations);
  for (int i = 0; i < iterations; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    fn();
    auto end = std::chrono::high_resolution_clock::now();
    times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
  }

  std::sort(times.begin(), times.end());
  double sum = std::accumulate(times.begin(), times.end(), 0.0);
  return {
      .avgMs = sum / iterations,
      .minMs = times.front(),
      .maxMs = times.back(),
      .medianMs = times[iterations / 2],
      .iterations = iterations,
  };
}

// =============================================================================
// Benchmark Result Types
// =============================================================================

struct BenchmarkResult {
  TimingStats timing;
  SearchStats search;
};

struct ComparisonResult {
  BenchmarkResult a;
  BenchmarkResult b;
  double speedupPercent;  // positive = B is faster

  static ComparisonResult from(const BenchmarkResult& a, const BenchmarkResult& b) {
    double speedup = (a.timing.avgMs - b.timing.avgMs) / a.timing.avgMs * 100.0;
    return {a, b, speedup};
  }
};

// =============================================================================
// Buffer Generation
// =============================================================================

enum class BufferShape { Uniform, Prose, CodeLike };

// Default range size for optimizeToRange() benchmarks
constexpr int DEFAULT_RANGE_SIZE = 6;

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
// Output Formatting - Basic
// =============================================================================

inline void printBanner(const std::string& title) {
  std::cout << "\n================================================================="
            << std::endl;
  std::cout << "                 " << title << std::endl;
  std::cout << "================================================================="
            << std::endl;
}

inline void printHeader(const std::string& title) {
  std::cout << "\n=== " << title << " ===" << std::endl;
}

inline void printSeparator() {
  std::cout << std::string(12, '-') << "|" << std::string(12, '-') << "|"
            << std::string(12, '-') << "|" << std::string(12, '-') << "|"
            << std::string(12, '-') << std::endl;
}

inline void printTableHeader(const std::string& paramName) {
  std::cout << std::left << std::setw(12) << paramName << "| " << std::right
            << std::setw(10) << "Avg (ms)"
            << " | " << std::setw(10) << "Min (ms)"
            << " | " << std::setw(10) << "Max (ms)"
            << " | " << std::setw(10) << "Median" << std::endl;
  printSeparator();
}

inline void printRow(const std::string& label, const TimingStats& stats) {
  std::cout << std::left << std::setw(12) << label << "| " << std::right
            << std::setw(10) << std::fixed << std::setprecision(2) << stats.avgMs
            << " | " << std::setw(10) << stats.minMs << " | " << std::setw(10)
            << stats.maxMs << " | " << std::setw(10) << stats.medianMs
            << std::endl;
}

// =============================================================================
// Output Formatting - With SearchStats
// =============================================================================

// Single-run table with stats (nodes, motions, stop reason)
void printTableHeaderWithStats(const std::string& paramName);
void printRowWithStats(const std::string& label, const BenchmarkResult& result);

// A/B comparison table
void printComparisonHeader(const std::string& paramName);
void printComparisonRow(const std::string& label, const ComparisonResult& cmp);

// =============================================================================
// Multi-Seed Benchmark Infrastructure
// =============================================================================
// Benchmarks average across multiple buffer seeds to avoid edge cases.
// Seeds are managed by SeedManager:
// - Fixed mode (default): deterministic seeds (42, 43, 44, ...)
// - Random mode: generate random seeds, logged to tests/.last_seeds.txt
// - Replay mode: read seeds from file for reproducing failures
//
// Enable random seeds: VIMFICIENCY_SEED_MODE=random ./build/tests/vimficiency_benchmarks
// Replay last seeds:   VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_benchmarks

constexpr int DEFAULT_SEED_COUNT = 5;
constexpr int DEFAULT_TIMING_ITERATIONS = 1;

// Helper to get seed mode description for output
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

// Aggregate multiple BenchmarkResults into one (averaging timing and stats)
// Star counts track how many runs triggered each stop condition
inline BenchmarkResult aggregateResults(const std::vector<BenchmarkResult>& results) {
  if (results.empty()) return {};

  // Collect all timing values
  std::vector<double> allTimes;
  for (const auto& r : results) {
    allTimes.push_back(r.timing.avgMs);
  }
  std::sort(allTimes.begin(), allTimes.end());

  double sum = std::accumulate(allTimes.begin(), allTimes.end(), 0.0);
  TimingStats timing{
      .avgMs = sum / allTimes.size(),
      .minMs = allTimes.front(),
      .maxMs = allTimes.back(),
      .medianMs = allTimes[allTimes.size() / 2],
      .iterations = static_cast<int>(allTimes.size()),
  };

  // Average search stats and count star occurrences
  SearchStats avgStats;
  for (const auto& r : results) {
    avgStats.nodesExplored += r.search.nodesExplored;
    avgStats.resultsFound += r.search.resultsFound;
    avgStats.queueSizeAtStop += r.search.queueSizeAtStop;
    if (r.search.uniquePositionsFound >= 0) {
      if (avgStats.uniquePositionsFound < 0) avgStats.uniquePositionsFound = 0;
      avgStats.uniquePositionsFound += r.search.uniquePositionsFound;
    }

    // Count stop reason occurrences for star markers
    switch (r.search.stopReason) {
      case SearchStopReason::MaxNodesReached:
        avgStats.searchedStarCount++;
        break;
      case SearchStopReason::MaxResultsFound:
        avgStats.foundStarCount++;
        break;
      case SearchStopReason::AllResultsFound:
        avgStats.uniqueStarCount++;
        break;
      case SearchStopReason::FullyExplored:
        avgStats.remainStarCount++;
        break;
      default:
        break;
    }
  }
  int n = static_cast<int>(results.size());
  avgStats.nodesExplored /= n;
  avgStats.resultsFound /= n;
  avgStats.queueSizeAtStop /= n;
  if (avgStats.uniquePositionsFound >= 0) avgStats.uniquePositionsFound /= n;

  // Use most common stop reason (or last one if all different)
  avgStats.stopReason = results.back().search.stopReason;

  return {timing, avgStats};
}

// =============================================================================
// A/B Comparison Framework
// =============================================================================
// Template for running benchmarks with paramsA/paramsB comparison.
// ParamsT: the params type (MotionOptimizerParams or EditOptimizerParams)
// SetupT: the benchmark setup type
// RunFn: function(SetupT, ParamsT) -> BenchmarkResult

template <bool EnableComparison, typename ParamsT, typename SetupT, typename RunFn>
void runUnifiedBenchmark(
    const std::string& label,
    const SetupT& setup,
    ParamsT paramsA,
    ParamsT paramsB,
    RunFn runFn) {
  if constexpr (EnableComparison) {
    BenchmarkResult a = runFn(setup, paramsA);
    BenchmarkResult b = runFn(setup, paramsB);
    printComparisonRow(label, ComparisonResult::from(a, b));
  } else {
    printRowWithStats(label, runFn(setup, paramsA));
  }
}

// Multi-seed version: runs benchmark across multiple seeds and aggregates results
// SetupFactory: function() -> SetupT (called after RandomGen::seed)
// Seeds are obtained from SeedManager (fixed, random, or replay mode)
template <bool EnableComparison, typename ParamsT, typename SetupT, typename SetupFactory, typename RunFn>
void runMultiSeedBenchmark(
    const std::string& label,
    SetupFactory setupFactory,
    ParamsT paramsA,
    ParamsT paramsB,
    RunFn runFn,
    int seedCount = DEFAULT_SEED_COUNT) {

  auto& seedMgr = SeedManager::instance();
  std::vector<BenchmarkResult> resultsA, resultsB;

  for (int i = 0; i < seedCount; i++) {
    int seed = seedMgr.getSeed(i);
    RandomGen::seed(seed);
    SetupT setup = setupFactory();
    resultsA.push_back(runFn(setup, paramsA));
    if constexpr (EnableComparison) {
      RandomGen::seed(seed);  // Reset seed for fair comparison
      setup = setupFactory();
      resultsB.push_back(runFn(setup, paramsB));
    }
  }

  if constexpr (EnableComparison) {
    BenchmarkResult a = aggregateResults(resultsA);
    BenchmarkResult b = aggregateResults(resultsB);
    printComparisonRow(label, ComparisonResult::from(a, b));
  } else {
    printRowWithStats(label, aggregateResults(resultsA));
  }
}

template <bool EnableComparison>
void printUnifiedHeader(const std::string& paramName) {
  if constexpr (EnableComparison) {
    printComparisonHeader(paramName);
  } else {
    printTableHeaderWithStats(paramName);
  }
}
