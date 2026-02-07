// tests/Benchmarks/EditOptimizerBench.cpp
//
// Performance benchmarks for EditOptimizer.
//
// Run: ./build/tests/vimficiency_benchmarks --gtest_filter="*EditOptimizer*"

#include <gtest/gtest.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/EditBoundary.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// A/B Comparison Configuration
// =============================================================================

constexpr bool ENABLE_COMPARISON = false;
constexpr const char* COMPARISON_NAME = "Default vs Experimental";

inline EditOptimizerParams paramsA() {
  EditOptimizerParams p;
  // Baseline configuration
  return p;
}

inline EditOptimizerParams paramsB() {
  EditOptimizerParams p;
  // Experimental configuration
  return p;
}

// =============================================================================
// Benchmark Infrastructure
// =============================================================================

class EditOptimizerBench : public ::testing::Test {
protected:
  static Config config;

  static void SetUpTestSuite() { RandomGen::seed(42); }

  struct BenchmarkSetup {
    Lines initialLines;
    Lines goalLines;
    EditBoundary boundary;

    // Pure deletion: delete all content
    BenchmarkSetup(const Lines& lines)
        : initialLines(lines),
          goalLines({""}),
          boundary(lines, Position(0, 0), lines.endPos()) {}

    // Custom goal
    BenchmarkSetup(const Lines& initial, const Lines& goal, const EditBoundary& b)
        : initialLines(initial), goalLines(goal), boundary(b) {}
  };

  static BenchmarkResult runWithParams(const BenchmarkSetup& cfg,
                                       const EditOptimizerParams& params,
                                       int iterations = 10) {
    EditOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          EditResult result = opt.optimizePureDeletion(cfg.initialLines, cfg.boundary, params);
          lastStats = result.stats;
        },
        iterations);

    return {timing, lastStats};
  }

  static void printRow(const string& label, const BenchmarkSetup& setup) {
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup, paramsA(), paramsB(),
        [](const BenchmarkSetup& s, const EditOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }

  static void printRowWithNodes(const string& label, const BenchmarkSetup& setup, int maxNodes) {
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup,
        paramsA().withMaxNodesExplored(maxNodes),
        paramsB().withMaxNodesExplored(maxNodes),
        [](const BenchmarkSetup& s, const EditOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }
};

Config EditOptimizerBench::config = Config::uniform();

// =============================================================================
// Benchmarks
// =============================================================================

TEST_F(EditOptimizerBench, BufferSize) {
  printBanner("EditOptimizer Benchmark Suite");
  if constexpr (ENABLE_COMPARISON) {
    cout << "Comparison: " << COMPARISON_NAME << endl;
  }
  printHeader("Buffer Size Benchmark (Pure Deletion)");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {1, 3, 5, 10, 15}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 30);
    printRow(to_string(numLines), BenchmarkSetup(lines));
  }
}

TEST_F(EditOptimizerBench, LineLength) {
  printHeader("Line Length Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Chars");

  for (int avgLen : {10, 20, 40, 60}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(5, avgLen);
    printRow(to_string(avgLen), BenchmarkSetup(lines));
  }
}

TEST_F(EditOptimizerBench, BufferShape) {
  printHeader("Buffer Shape Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Shape");

  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(10, 30, shape);
    printRow(name, BenchmarkSetup(lines));
  }
}

TEST_F(EditOptimizerBench, SearchDepth) {
  printHeader("Search Depth Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Depth");

  RandomGen::seed(42);
  Lines lines = generateBuffer(15, 30);
  BenchmarkSetup setup(lines);

  for (int depth : {1000, 5000, 10000, 50000}) {
    printRowWithNodes(to_string(depth), setup, depth);
  }
}

TEST_F(EditOptimizerBench, MultiLineDelete) {
  printHeader("Multi-Line Deletion Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {2, 4, 6, 8, 10}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 20);
    printRow(to_string(numLines), BenchmarkSetup(lines));
  }
}

TEST_F(EditOptimizerBench, BoundaryConstraints) {
  printHeader("Boundary Constraints Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Boundary");

  RandomGen::seed(42);
  Lines fullBuffer = generateBuffer(10, 30);

  // Full buffer (no boundary constraints)
  printRow("None", BenchmarkSetup(fullBuffer));

  // With prefix (first half of first line is protected)
  {
    Position firstPos(0, 15);
    Position endPos = fullBuffer.endPos();
    Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
    EditBoundary boundary(fullBuffer, firstPos, endPos);
    printRow("Prefix", BenchmarkSetup(editRegion, {""}, boundary));
  }

  // With suffix (second half of last line is protected)
  {
    Position firstPos(0, 0);
    int lastLine = fullBuffer.lastLine();
    int midCol = static_cast<int>(fullBuffer[lastLine].size()) / 2;
    Position endPos(lastLine, midCol + 1);
    Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
    EditBoundary boundary(fullBuffer, firstPos, endPos);
    printRow("Suffix", BenchmarkSetup(editRegion, {""}, boundary));
  }

  // With both prefix and suffix
  {
    Position firstPos(0, 10);
    int lastLine = fullBuffer.lastLine();
    int lastLineLen = static_cast<int>(fullBuffer[lastLine].size());
    Position endPos(lastLine, max(1, lastLineLen - 10 + 1));
    Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
    EditBoundary boundary(fullBuffer, firstPos, endPos);
    printRow("Both", BenchmarkSetup(editRegion, {""}, boundary));
  }
}
