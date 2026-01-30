// tests/Benchmarks/MotionOptimizerBench.cpp
//
// Performance benchmarks for MotionOptimizer across different dimensions.
// Run with: ./vimficiency_benchmarks

#include <gtest/gtest.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/MotionOptimizer.h"
#include "Optimizer/OptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "State/RunningEffort.h"
#include "Utils/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

class MotionOptimizerBench : public ::testing::Test {
protected:
  static Config config;
  static NavContext navContext;

  static void SetUpTestSuite() { RandomGen::seed(12); }

  // ================== Buffer Generation ==================
  enum class BufferShape { Uniform, CodeLike };

  static Lines generateBuffer(int numLines, int avgLineLen, BufferShape shape) {
    if (shape == BufferShape::Uniform) {
      return randomProseLines(numLines, avgLineLen, avgLineLen);
    } else {
      return randomCodeLines(numLines, avgLineLen);
    }
  }

  // ================== Benchmark Execution ==================
  struct BenchmarkSetup {
    Lines lines;
    MotionBoundary boundary{};
    Position firstPos;
    Position lastPos;
    OptimizerParams params{};

    BenchmarkSetup(const Lines& lines) : lines(lines) {
      firstPos = randomFirstPos(lines);
      lastPos = randomLastPos(lines);
      boundary = MotionBoundary(lines, firstPos, lastPos);
    }
  };

  struct BenchmarkResult {
    TimingStats timing;
    SearchStats search;
  };

  static BenchmarkResult runBenchmark(const BenchmarkSetup& cfg, int iterations = 10) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimize(cfg.lines, cfg.firstPos, RunningEffort(), cfg.lastPos, "",
                       navContext, cfg.boundary, EXPLORABLE_MOTIONS, cfg.params);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  static void printRowWithStats(const string& label, const BenchmarkResult& result) {
    cout << setw(12) << left << label
         << "|" << setw(11) << right << fixed << setprecision(2) << result.timing.avgMs
         << " |" << setw(11) << result.timing.minMs
         << " |" << setw(11) << result.timing.maxMs
         << " |" << setw(11) << result.timing.medianMs
         << " |" << setw(8) << result.search.nodesExplored
         << " | " << to_string(result.search.stopReason)
         << endl;
  }

  static void printTableHeaderWithStats(const string& paramName) {
    cout << setw(12) << left << paramName
         << "|" << setw(11) << right << "Avg (ms)"
         << " |" << setw(11) << "Min (ms)"
         << " |" << setw(11) << "Max (ms)"
         << " |" << setw(11) << "Median"
         << " |" << setw(8) << "Nodes"
         << " | " << "StopReason"
         << endl;
    cout << string(12, '-') << "|" << string(12, '-')
         << "|" << string(12, '-') << "|" << string(12, '-')
         << "|" << string(12, '-') << "|" << string(9, '-')
         << "|" << string(20, '-') << endl;
  }
};

Config MotionOptimizerBench::config = Config::uniform();
NavContext MotionOptimizerBench::navContext;


TEST_F(MotionOptimizerBench, BufferSize) {
  printBanner("MotionOptimizer Benchmark Suite");
  printHeader("Buffer Size Benchmark");
  printTableHeader("Lines");

  vector<int> lineCounts = {1, 5, 10, 15, 20, 30};
  const int avgLineLen = 20;

  for (int numLines : lineCounts) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, avgLineLen, BufferShape::Uniform);
    BenchmarkResult res = runBenchmark(BenchmarkSetup(lines));
    printRow(to_string(numLines), res.timing);
  }
}

TEST_F(MotionOptimizerBench, LineLength) {
  printHeader("Line Length Benchmark");
  printTableHeader("Chars");

  vector<int> lineLengths = {10, 20, 40, 60, 80};
  const int numLines = 15;

  for (int avgLen : lineLengths) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, avgLen, BufferShape::Uniform);
    printRow(to_string(avgLen), runBenchmark(BenchmarkSetup(lines)).timing);
  }
}

TEST_F(MotionOptimizerBench, BufferShape) {
  printHeader("Buffer Shape Benchmark");
  printTableHeader("Shape");

  const int numLines = 20;
  const int avgLineLen = 30;

  vector<pair<string, BufferShape>> shapes = {
      {"Uniform", BufferShape::Uniform},
      {"CodeLike", BufferShape::CodeLike},
  };

  for (const auto& [name, shape] : shapes) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, avgLineLen, shape);
    printRow(name, runBenchmark(BenchmarkSetup(lines)).timing);
  }
}

TEST_F(MotionOptimizerBench, BoundarySettings) {
  printHeader("Boundary Settings Benchmark");
  printTableHeader("Boundary");

  const int numLines = 20;
  const int avgLineLen = 30;

  RandomGen::seed(42);
  Lines lines = generateBuffer(numLines, avgLineLen, BufferShape::Uniform);
  // todo
}

// =============================================================================
// Benchmark: Search Depth
// =============================================================================

TEST_F(MotionOptimizerBench, SearchDepth) {
  printHeader("Search Depth Benchmark");
  printTableHeaderWithStats("Depth");

  // Larger buffer + more results + higher exploreFactor so that
  // node limit (depth) becomes the limiting factor, not maxResults
  RandomGen::seed(42);
  Lines lines = generateBuffer(50, 30, BufferShape::Uniform);

  for (int depth : {1000, 5000, 10000, 50000, 100000}) {
    BenchmarkSetup setup(lines);
    setup.params = {.maxResults = 500, .maxNodesExplored = depth, .exploreFactor = 3.0};
    printRowWithStats(to_string(depth), runBenchmark(setup));
  }
}
