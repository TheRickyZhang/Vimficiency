// tests/Benchmarks/MotionOptimizerBench.cpp
//
// Performance benchmarks for MotionOptimizer.
//
// Run: ./build/tests/vimficiency_benchmarks --gtest_filter="*MotionOptimizer*"

#include <gtest/gtest.h>
#include <set>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizerParams.h"
#include "State/RunningEffort.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// A/B Comparison Configuration
// =============================================================================

constexpr bool ENABLE_COMPARISON = true;
constexpr const char* COMPARISON_NAME = "Directional Pruning (A = on, B = off)";

// NOTE: Must be static, not inline - inline causes incorrect behavior with templates
static MotionOptimizerParams paramsA() {
  return MotionOptimizerParams{};  // default: useDirectionalPruning = true
}

static MotionOptimizerParams paramsB() {
  return MotionOptimizerParams{}.withDirectionalPruning(false);
}

static MotionOptimizerRangeParams rangeParamsA() {
  return MotionOptimizerRangeParams{};  // default: useDirectionalPruning = true
}

static MotionOptimizerRangeParams rangeParamsB() {
  return MotionOptimizerRangeParams{}.withDirectionalPruning(false);
}

// =============================================================================
// Benchmark Infrastructure
// =============================================================================

class MotionOptimizerBench : public ::testing::Test {
protected:
  static Config config;
  static NavContext navContext;

  static void SetUpTestSuite() { RandomGen::seed(12); }

  // ================== Setup Types ==================
  struct BenchmarkSetup {
    Lines lines;
    MotionBoundary boundary{};
    Position firstPos;
    Position lastPos;

    BenchmarkSetup(const Lines& lines) : lines(lines) {
      firstPos = randomFirstPos(lines);
      lastPos = randomLastPos(lines);
      boundary = MotionBoundary(lines, firstPos, lastPos, true, true);
    }
  };

  struct RangeBenchmarkSetup {
    Lines lines;
    MotionBoundary boundary{};
    Position startPos;
    Position rangeFirst;
    Position rangeLast;

    // Default constructor: uses DEFAULT_RANGE_SIZE (6) columns at end of last line
    RangeBenchmarkSetup(const Lines& lines) : lines(lines) {
      startPos = {0, 0};
      int lastLine = lines.lastLine();
      int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
      int rangeSize = min(DEFAULT_RANGE_SIZE, lastLineLen);
      int startCol = max(0, lastLineLen - rangeSize);
      rangeFirst = {lastLine, startCol};
      rangeLast = {lastLine, lastLineLen - 1};
      boundary = MotionBoundary(lines, rangeFirst, rangeLast, true, true);
    }

    RangeBenchmarkSetup(const Lines& lines, int rangeChars, int rangeLines = 1) : lines(lines) {
      startPos = {0, 0};
      int lastLine = lines.lastLine();
      int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
      rangeLines = min(rangeLines, lastLine + 1);

      if (rangeLines == 1) {
        int actualChars = min(rangeChars, lastLineLen);
        int startCol = max(0, lastLineLen - actualChars);
        rangeFirst = {lastLine, startCol};
        rangeLast = {lastLine, lastLineLen - 1};
      } else {
        int firstLine = lastLine - rangeLines + 1;
        int firstLineLen = max(1, static_cast<int>(lines[firstLine].size()));
        int startCol = max(0, firstLineLen / 2);
        rangeFirst = {firstLine, startCol};
        rangeLast = {lastLine, lastLineLen - 1};
      }
      boundary = MotionBoundary(lines, rangeFirst, rangeLast, true, true);
    }
  };

  // ================== Run Functions ==================
  static BenchmarkResult runWithParams(const BenchmarkSetup& cfg,
                                       const MotionOptimizerParams& params,
                                       int iterations = 5) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimize(cfg.lines, cfg.firstPos, RunningEffort(),
                                               cfg.lastPos, "", navContext, cfg.boundary,
                                               EXPLORABLE_MOTIONS, params);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  static BenchmarkResult runRangeWithParams(const RangeBenchmarkSetup& cfg,
                                            const MotionOptimizerRangeParams& params,
                                            int iterations = 5) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimizeToRange(
              cfg.lines, cfg.startPos, RunningEffort(),
              cfg.rangeFirst, cfg.rangeLast, "",
              navContext, cfg.boundary, EXPLORABLE_MOTIONS, params);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  // ================== Print Helpers ==================
  static void printRow(const string& label, const BenchmarkSetup& setup) {
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup, paramsA(), paramsB(),
        [](const BenchmarkSetup& s, const MotionOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }

  static void printRangeRow(const string& label, const RangeBenchmarkSetup& setup) {
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup, rangeParamsA(), rangeParamsB(),
        [](const RangeBenchmarkSetup& s, const MotionOptimizerRangeParams& p) {
          return runRangeWithParams(s, p);
        });
  }

  static void printRowWithNodes(const string& label, const BenchmarkSetup& setup,
                                int maxNodes, double exploreFactor = 3.0) {
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup,
        paramsA().withMaxNodesExplored(maxNodes).withExploreFactor(exploreFactor),
        paramsB().withMaxNodesExplored(maxNodes).withExploreFactor(exploreFactor),
        [](const BenchmarkSetup& s, const MotionOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }

  // ================== Debug Utilities ==================
  static void printDetailedComparison(const BenchmarkSetup& cfg) {
    cout << "\n--- Detailed Comparison ---\n";
    cout << "Start: (" << cfg.firstPos.line << ", " << cfg.firstPos.col << ")\n";
    cout << "Goal:  (" << cfg.lastPos.line << ", " << cfg.lastPos.col << ")\n";
    cout << "Lines: " << cfg.lines.size() << "\n\n";

    auto runDetailed = [&](MotionOptimizerParams params) {
      params.trackExploredStates = true;
      MotionOptimizer opt(config);
      return opt.optimize(cfg.lines, cfg.firstPos, RunningEffort(), cfg.lastPos, "",
                          navContext, cfg.boundary, EXPLORABLE_MOTIONS, params);
    };

    auto [resA, statsA] = runDetailed(paramsA());
    auto [resB, statsB] = runDetailed(paramsB());

    auto printResults = [](const vector<Result>& res, const SearchStats& stats, const char* name) {
      cout << name << ": nodes=" << stats.nodesExplored
           << " motions=" << stats.motionsEmitted
           << " skipped=" << stats.statesSkipped
           << " avg=" << fixed << setprecision(1) << stats.avgMotionsPerState() << "/state\n";
      cout << "   Results (" << res.size() << "):\n";
      for (size_t i = 0; i < min(res.size(), size_t(5)); i++) {
        cout << "     \"" << res[i].getSequenceString() << "\" (cost="
             << setprecision(2) << res[i].keyCost << ")\n";
      }
    };

    printResults(resA, statsA, "A");
    cout << "\n";
    printResults(resB, statsB, "B");

    // Compare explored states
    if (!statsA.exploredStates.empty() && !statsB.exploredStates.empty()) {
      set<pair<int,int>> posA, posB;
      for (const auto& s : statsA.exploredStates) posA.insert({s.line, s.col});
      for (const auto& s : statsB.exploredStates) posB.insert({s.line, s.col});

      vector<pair<int,int>> onlyInA, onlyInB;
      for (const auto& p : posA) if (posB.find(p) == posB.end()) onlyInA.push_back(p);
      for (const auto& p : posB) if (posA.find(p) == posA.end()) onlyInB.push_back(p);

      cout << "\nState differences:\n";
      cout << "  Positions explored: A=" << posA.size() << ", B=" << posB.size()
           << ", shared=" << (posA.size() - onlyInA.size()) << "\n";
    }
    cout << "---\n";
  }
};

Config MotionOptimizerBench::config = Config::uniform();
NavContext MotionOptimizerBench::navContext;

// =============================================================================
// Benchmarks
// =============================================================================

TEST_F(MotionOptimizerBench, BufferSize) {
  printBanner("MotionOptimizer Benchmark Suite");
  if constexpr (ENABLE_COMPARISON) {
    cout << "Comparison: " << COMPARISON_NAME << endl;
  }
  printHeader("Buffer Size Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {1, 5, 10, 15, 20, 30}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 30);
    printRow(to_string(numLines), BenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, LineLength) {
  printHeader("Line Length Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Chars");

  for (int avgLen : {10, 20, 40, 60, 80}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(20, avgLen);
    printRow(to_string(avgLen), BenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, BufferShape) {
  printHeader("Buffer Shape Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Shape");

  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(20, 30, shape);
    printRow(name, BenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, RangePruning) {
  printHeader("Range Optimization Benchmark (optimizeToRange)");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {5, 10, 20, 30, 40}) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 30);
    printRangeRow(to_string(numLines), RangeBenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, RangeSize) {
  printHeader("Range Size Benchmark (fixed 20-line buffer)");
  printUnifiedHeader<ENABLE_COMPARISON>("Range");

  RandomGen::seed(42);
  Lines lines = generateBuffer(20, 30);

  for (const auto& [label, chars, numLines] : vector<tuple<string, int, int>>{
           {"1 col", 1, 1},
           {"3 cols", 3, 1},
           {"6 cols", 6, 1},
           {"10 cols", 10, 1},
           {"30 (2 ln)", 30, 2}}) {
    printRangeRow(label, RangeBenchmarkSetup(lines, chars, numLines));
  }
}

TEST_F(MotionOptimizerBench, SearchDepth) {
  printHeader("Search Depth Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Depth");

  RandomGen::seed(42);
  Lines lines = generateBuffer(40, 30);
  BenchmarkSetup setup(lines);

  for (int depth : {1000, 5000, 10000, 50000, 100000}) {
    printRowWithNodes(to_string(depth), setup, depth);
  }
}

TEST_F(MotionOptimizerBench, DISABLED_DetailedComparison) {
  printHeader("Detailed Single-Case Comparison");

  RandomGen::seed(42);
  Lines lines1 = generateBuffer(20, 30, BufferShape::Uniform);
  printDetailedComparison(BenchmarkSetup(lines1));

  RandomGen::seed(42);
  Lines lines2 = generateBuffer(40, 30, BufferShape::CodeLike);
  printDetailedComparison(BenchmarkSetup(lines2));
}
