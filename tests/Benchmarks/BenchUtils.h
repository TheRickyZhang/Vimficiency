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
// Output Formatting
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
