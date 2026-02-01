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

inline Lines generateBuffer(int numLines, int avgLineLen, BufferShape shape = BufferShape::CodeLike) {
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

template <bool EnableComparison>
void printUnifiedHeader(const std::string& paramName) {
  if constexpr (EnableComparison) {
    printComparisonHeader(paramName);
  } else {
    printTableHeaderWithStats(paramName);
  }
}
