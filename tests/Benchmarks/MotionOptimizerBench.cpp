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
      return generateCodeLikeBuffer(numLines, avgLineLen);
    }
  }

  static Lines generateCodeLikeBuffer(int numLines, int avgLineLen) {
    Lines result;
    result.reserve(numLines);

    for (int i = 0; i < numLines; i++) {
      if (RandomGen::chance(1, 10)) {
        result.push_back("");
        continue;
      }

      string line;
      line += string(RandomGen::range(0, 4), ' ');

      for(int i = 0; i < RandomGen::range(1, avgLineLen * 2); i++) {
        line += RandomGen::pick<std::string_view>({
            {3, CharPools::KEYWORDS},
            {1, CharPools::SYMBOLS},
            {1, CharPools::SPACE},
        });
      }
      result.push_back(line);
    }
    return result;
  }

  // ================== Benchmark Execution ==================
  struct BenchmarkConfig {
    Lines lines;
    Position start{0, 0};
    Position end;
    MotionBoundary boundary{};
    OptimizerParams params{.maxNodesExplored = 50000, .exploreFactor = 2.0};
  };

  // Helper for the common case: start at origin, end at last position
  static BenchmarkConfig makeConfig(const Lines& lines, OptimizerParams params = {}) {
    return {.lines = lines, .end = lines.lastPos(), .params = params};
  }

  struct BenchmarkResult {
    TimingStats timing;
    SearchStats search;
  };

  static BenchmarkResult runBenchmark(const BenchmarkConfig& cfg, int iterations = 10) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimize(cfg.lines, cfg.start, RunningEffort(), cfg.end, "",
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
    BenchmarkResult res = runBenchmark(makeConfig(lines));
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
    printRow(to_string(avgLen), runBenchmark(makeConfig(lines)).timing);
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
    printRow(name, runBenchmark(makeConfig(lines)).timing);
  }
}

TEST_F(MotionOptimizerBench, BoundarySettings) {
  printHeader("Boundary Settings Benchmark");
  printTableHeader("Boundary");

  const int numLines = 20;
  const int avgLineLen = 30;

  RandomGen::seed(42);
  Lines lines = generateBuffer(numLines, avgLineLen, BufferShape::Uniform);

  struct BoundaryCase {
    string name;
    bool hasLinesAbove;
    bool hasLinesBelow;
  };

  vector<BoundaryCase> cases = {
      {"None", false, false},
      {"Above", true, false},
      {"Below", false, true},
      {"Both", true, true},
  };

  for (const auto& bc : cases) {
    MotionBoundary boundary(lines, {0, 0}, lines.lastPos(), bc.hasLinesAbove, bc.hasLinesBelow);
    BenchmarkConfig cfg{
        .lines = lines,
        .start = {numLines / 2, 0},
        .end = lines.lastPos(),
        .boundary = boundary,
    };
    printRow(bc.name, runBenchmark(cfg).timing);
  }
}

// =============================================================================
// Benchmark: Search Depth
// =============================================================================

TEST_F(MotionOptimizerBench, SearchDepth) {
  printHeader("Search Depth Benchmark");
  printTableHeaderWithStats("Depth");

  RandomGen::seed(42);
  Lines lines = generateBuffer(15, 20, BufferShape::Uniform);

  for (int depth : {1000, 5000, 10000, 50000, 100000}) {
    printRowWithStats(to_string(depth), runBenchmark(makeConfig(lines, {.maxNodesExplored = depth, .exploreFactor = 2.0})));
  }
}
