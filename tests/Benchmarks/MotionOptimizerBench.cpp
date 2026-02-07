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
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionOptimizerParams.h"
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
      Position boundaryEnd(lastPos.line, lastPos.col + 1);
      boundary = MotionBoundary(lines, firstPos, boundaryEnd, true, true);
    }
  };

  struct RangeBenchmarkSetup {
    Lines lines;
    MotionBoundary boundary{};
    Position initialPos;
    Position rangeFirst;
    Position rangeEnd;

    // Default constructor: uses DEFAULT_RANGE_SIZE (6) columns at end of last line
    RangeBenchmarkSetup(const Lines& lines) : lines(lines) {
      initialPos = {0, 0};
      int lastLine = lines.lastLine();
      int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
      int rangeSize = min(DEFAULT_RANGE_SIZE, lastLineLen);
      int startCol = max(0, lastLineLen - rangeSize);
      rangeFirst = {lastLine, startCol};
      rangeEnd = {lastLine, lastLineLen};
      boundary = MotionBoundary(lines, rangeFirst, rangeEnd, true, true);
    }

    RangeBenchmarkSetup(const Lines& lines, int rangeChars, int rangeLines = 1) : lines(lines) {
      initialPos = {0, 0};
      int lastLine = lines.lastLine();
      int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
      rangeLines = min(rangeLines, lastLine + 1);

      if (rangeLines == 1) {
        int actualChars = min(rangeChars, lastLineLen);
        int startCol = max(0, lastLineLen - actualChars);
        rangeFirst = {lastLine, startCol};
        rangeEnd = {lastLine, lastLineLen};
      } else {
        int firstLine = lastLine - rangeLines + 1;
        int firstLineLen = max(1, static_cast<int>(lines[firstLine].size()));
        int startCol = max(0, firstLineLen / 2);
        rangeFirst = {firstLine, startCol};
        rangeEnd = {lastLine, lastLineLen};
      }
      boundary = MotionBoundary(lines, rangeFirst, rangeEnd, true, true);
    }
  };

  // ================== Run Functions ==================
  // Use 2 iterations per seed (warmup handles cache effects)
  static BenchmarkResult runWithParams(const BenchmarkSetup& cfg,
                                       const MotionOptimizerParams& params,
                                       int iterations = DEFAULT_TIMING_ITERATIONS) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimize(cfg.lines, cfg.firstPos, cfg.lastPos,
                                               params, "", cfg.boundary);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  static BenchmarkResult runRangeWithParams(const RangeBenchmarkSetup& cfg,
                                            const MotionOptimizerRangeParams& params,
                                            int iterations = DEFAULT_TIMING_ITERATIONS) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimizeToRange(
              cfg.lines, cfg.initialPos, cfg.rangeFirst, cfg.rangeEnd,
              params, "", cfg.boundary);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  // ================== Print Helpers (Single Setup) ==================
  // For debugging specific setups
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

  // ================== Print Helpers (Multi-Seed) ==================
  // Averages across 5 different buffer seeds for representative results
  static void printMultiSeedRow(const string& label, int numLines, int avgLen = 30,
                                BufferShape shape = BufferShape::CodeLike) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, MotionOptimizerParams, BenchmarkSetup>(
        label,
        [=]() { return BenchmarkSetup(generateBuffer(numLines, avgLen, shape)); },
        paramsA(), paramsB(),
        [](const BenchmarkSetup& s, const MotionOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }

  static void printMultiSeedRangeRow(const string& label, int numLines, int avgLen = 30) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, MotionOptimizerRangeParams, RangeBenchmarkSetup>(
        label,
        [=]() { return RangeBenchmarkSetup(generateBuffer(numLines, avgLen)); },
        rangeParamsA(), rangeParamsB(),
        [](const RangeBenchmarkSetup& s, const MotionOptimizerRangeParams& p) {
          return runRangeWithParams(s, p);
        });
  }

  // Multi-seed range with explicit range size (for RangeResultSize benchmark)
  static void printMultiSeedRangeSizeRow(const string& label, int rangeChars, int rangeLines = 1,
                                         int bufferLines = 20, int avgLen = 30) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, MotionOptimizerRangeParams, RangeBenchmarkSetup>(
        label,
        [=]() { return RangeBenchmarkSetup(generateBuffer(bufferLines, avgLen), rangeChars, rangeLines); },
        rangeParamsA(), rangeParamsB(),
        [](const RangeBenchmarkSetup& s, const MotionOptimizerRangeParams& p) {
          return runRangeWithParams(s, p);
        });
  }

  static void printMultiSeedRowWithNodes(const string& label, int numLines, int avgLen,
                                         int maxNodes, double exploreFactor = 3.0,
                                         BufferShape shape = BufferShape::CodeLike) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, MotionOptimizerParams, BenchmarkSetup>(
        label,
        [=]() { return BenchmarkSetup(generateBuffer(numLines, avgLen, shape)); },
        paramsA().withMaxNodesExplored(maxNodes).withExploreFactor(exploreFactor),
        paramsB().withMaxNodesExplored(maxNodes).withExploreFactor(exploreFactor),
        [](const BenchmarkSetup& s, const MotionOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }

  // Legacy single-setup helper (kept for specific node limit tests)
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
  static void printSetup(const BenchmarkSetup& cfg) {
    cout << "\n--- Setup Debug ---\n";
    cout << "Start: (" << cfg.firstPos.line << ", " << cfg.firstPos.col << ")\n";
    cout << "Goal:  (" << cfg.lastPos.line << ", " << cfg.lastPos.col << ")\n";
    cout << "Boundary: hasLinesAbove=" << cfg.boundary.hasLinesAbove()
         << ", hasLinesBelow=" << cfg.boundary.hasLinesBelow() << "\n";
    cout << "Lines (" << cfg.lines.size() << "):\n";
    for (size_t i = 0; i < cfg.lines.size(); i++) {
      cout << "  " << i << ": \"" << cfg.lines[i] << "\"\n";
    }
    cout << "---\n\n";
  }

  static void printSetupWithResults(const BenchmarkSetup& cfg, const MotionOptimizerParams& params) {
    printSetup(cfg);
    MotionOptimizer opt(config);
    auto [results, stats] = opt.optimize(cfg.lines, cfg.firstPos, cfg.lastPos,
                                         params, "", cfg.boundary);
    cout << "Results found (" << results.size() << "):\n";
    for (size_t i = 0; i < min(results.size(), size_t(10)); i++) {
      cout << "  " << i << ": \"" << results[i].getSequenceString()
           << "\" (cost=" << fixed << setprecision(2) << results[i].keyCost << ")\n";
    }
    cout << "Stats: nodes=" << stats.nodesExplored << ", results=" << stats.resultsFound << "\n";
    cout << "---\n\n";
  }

  static void printRangeSetup(const RangeBenchmarkSetup& cfg) {
    cout << "\n--- Range Setup Debug ---\n";
    cout << "Start: (" << cfg.initialPos.line << ", " << cfg.initialPos.col << ")\n";
    cout << "Range: (" << cfg.rangeFirst.line << ", " << cfg.rangeFirst.col << ") to ("
         << cfg.rangeEnd.line << ", " << cfg.rangeEnd.col << ")\n";
    cout << "Lines (" << cfg.lines.size() << "):\n";
    for (size_t i = 0; i < cfg.lines.size(); i++) {
      cout << "  " << i << ": \"" << cfg.lines[i] << "\"\n";
    }
    cout << "---\n\n";
  }

  static void printDetailedComparison(const BenchmarkSetup& cfg) {
    cout << "\n--- Detailed Comparison ---\n";
    cout << "Start: (" << cfg.firstPos.line << ", " << cfg.firstPos.col << ")\n";
    cout << "Goal:  (" << cfg.lastPos.line << ", " << cfg.lastPos.col << ")\n";
    cout << "Lines: " << cfg.lines.size() << "\n\n";

    auto runDetailed = [&](MotionOptimizerParams params) {
      params.trackExploredStates = true;
      MotionOptimizer opt(config);
      return opt.optimize(cfg.lines, cfg.firstPos, cfg.lastPos, params,
                          "", cfg.boundary);
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
  cout << "(Averaged across " << DEFAULT_SEED_COUNT << " seeds - "
       << getSeedModeDescription() << ")\n";
  printHeader("Buffer Size Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {1, 5, 10, 15, 20, 30}) {
    printMultiSeedRow(to_string(numLines), numLines, 30);
  }
}

TEST_F(MotionOptimizerBench, LineLength) {
  printHeader("Line Length Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Chars");

  for (int avgLen : {10, 20, 40, 60, 80}) {
    printMultiSeedRow(to_string(avgLen), 20, avgLen);
  }
}

TEST_F(MotionOptimizerBench, BufferShape) {
  printHeader("Buffer Shape Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Shape");

  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    printMultiSeedRow(name, 20, 30, shape);
  }
}


// Disabled since currently always finds max results in less than search depth. Might make more sense to just look at the depth / search time ratio from other tests.
TEST_F(MotionOptimizerBench, DISABLED_SearchDepth) {
  printHeader("Search Depth Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Depth");

  for (int depth : {1000, 5000, 10000, 50000, 100000}) {
    printMultiSeedRowWithNodes(to_string(depth), 20, 30, depth, 3.0);
  }
}

TEST_F(MotionOptimizerBench, RangeBufferLines) {
  printHeader("Range Optimization Benchmark (optimizeToRange)");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {5, 10, 20, 30, 40}) {
    printMultiSeedRangeRow(to_string(numLines), numLines, 30);
  }
}

TEST_F(MotionOptimizerBench, RangeResultSize) {
  printHeader("Range Size Benchmark (20-line buffer, varying range size)");
  printUnifiedHeader<ENABLE_COMPARISON>("Range");

  for (const auto& [label, chars, rangeLines] : vector<tuple<string, int, int>>{
           {"1 col", 1, 1},
           {"3 cols", 3, 1},
           {"6 cols", 6, 1},
           {"10 cols", 10, 1},
           {"30 (2 ln)", 30, 2}}) {
    printMultiSeedRangeSizeRow(label, chars, rangeLines);
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
