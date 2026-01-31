// tests/Benchmarks/MotionOptimizerBench.cpp
//
// Performance benchmarks for MotionOptimizer across different dimensions.
// Run with: ./vimficiency_benchmarks

#include <gtest/gtest.h>
#include <map>
#include <set>

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

// =============================================================================
// A/B Comparison Configuration
// =============================================================================
// Set ENABLE_COMPARISON to true to compare two implementations side-by-side.
// Modify paramsA() and paramsB() to configure what differs between them.
// When false, runs normal single-implementation benchmarks using paramsA().

constexpr bool ENABLE_COMPARISON = true;
constexpr const char* COMPARISON_NAME = "Directional Pruning (A = off, B = on)";

inline OptimizerParams paramsA() {
  OptimizerParams p;
  p.useDirectionalPruning = false;  // Baseline (current default)
  return p;
}

inline OptimizerParams paramsB() {
  OptimizerParams p;
  p.useDirectionalPruning = true;   // Experimental
  return p;
}

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

    BenchmarkSetup(const Lines& lines) : lines(lines) {
      firstPos = randomFirstPos(lines);
      lastPos = randomLastPos(lines);
      boundary = MotionBoundary(lines, firstPos, lastPos, true, true);
    }
  };

  struct BenchmarkResult {
    TimingStats timing;
    SearchStats search;
  };

  struct ComparisonResult {
    BenchmarkResult a;
    BenchmarkResult b;
    double speedupPercent;  // positive = B is faster
  };

  static BenchmarkResult runBenchmarkWithParams(const BenchmarkSetup& cfg,
                                                 const OptimizerParams& params,
                                                 int iterations = 10) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimize(cfg.lines, cfg.firstPos, RunningEffort(), cfg.lastPos, "",
                       navContext, cfg.boundary, EXPLORABLE_MOTIONS, params);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  // Runs benchmark with paramsA() - use for non-comparison mode or as baseline
  static BenchmarkResult runBenchmark(const BenchmarkSetup& cfg, int iterations = 10) {
    return runBenchmarkWithParams(cfg, paramsA(), iterations);
  }

  // Runs A/B comparison benchmark
  static ComparisonResult runComparison(const BenchmarkSetup& cfg, int iterations = 10) {
    BenchmarkResult a = runBenchmarkWithParams(cfg, paramsA(), iterations);
    BenchmarkResult b = runBenchmarkWithParams(cfg, paramsB(), iterations);
    double speedup = (a.timing.avgMs - b.timing.avgMs) / a.timing.avgMs * 100.0;
    return {a, b, speedup};
  }

  // ================== Range Benchmark Helpers ==================

  struct RangeBenchmarkSetup {
    Lines lines;
    MotionBoundary boundary{};
    Position startPos;   // Starting position for optimization
    Position rangeFirst; // First position in target range
    Position rangeLast;  // Last position in target range

    // Random range near end of buffer
    RangeBenchmarkSetup(const Lines& lines) : lines(lines) {
      startPos = {0, 0};
      // Place range on last line with random column range
      int lastLine = lines.lastLine();
      int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
      int col1 = RandomGen::range(0, lastLineLen - 1);
      int col2 = RandomGen::range(0, lastLineLen - 1);
      rangeFirst = {lastLine, min(col1, col2)};
      rangeLast = {lastLine, max(col1, col2)};
      boundary = MotionBoundary(lines, rangeFirst, rangeLast, true, true);
    }

    // Controlled range size: place range near end of buffer
    // rangeChars: number of characters in range
    // rangeLines: number of lines the range spans (1 = same line)
    RangeBenchmarkSetup(const Lines& lines, int rangeChars, int rangeLines = 1) : lines(lines) {
      startPos = {0, 0};

      int lastLine = lines.lastLine();
      int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));

      // Clamp rangeLines to available lines
      rangeLines = min(rangeLines, lastLine + 1);

      if (rangeLines == 1) {
        // Same-line range on last line
        int actualChars = min(rangeChars, lastLineLen);
        int startCol = max(0, lastLineLen - actualChars);
        rangeFirst = {lastLine, startCol};
        rangeLast = {lastLine, lastLineLen - 1};
      } else {
        // Multi-line range ending at last line
        int firstLine = lastLine - rangeLines + 1;
        int firstLineLen = max(1, static_cast<int>(lines[firstLine].size()));
        // Start partway through the first line of range
        int startCol = max(0, firstLineLen / 2);
        rangeFirst = {firstLine, startCol};
        rangeLast = {lastLine, lastLineLen - 1};
      }

      boundary = MotionBoundary(lines, rangeFirst, rangeLast, true, true);
    }
  };

  static BenchmarkResult runRangeBenchmarkWithParams(const RangeBenchmarkSetup& cfg,
                                                      const OptimizerParams& params,
                                                      int iterations = 10) {
    MotionOptimizer opt(config);
    SearchStats lastStats;

    TimingStats timing = measureTiming(
        [&]() {
          auto [results, stats] = opt.optimizeToRange(
              cfg.lines, cfg.startPos, RunningEffort(),
              cfg.rangeFirst, cfg.rangeLast, "",
              navContext, false, cfg.boundary, EXPLORABLE_MOTIONS, params);
          lastStats = stats;
        },
        iterations);

    return {timing, lastStats};
  }

  static ComparisonResult runRangeComparison(const RangeBenchmarkSetup& cfg, int iterations = 10) {
    BenchmarkResult a = runRangeBenchmarkWithParams(cfg, paramsA(), iterations);
    BenchmarkResult b = runRangeBenchmarkWithParams(cfg, paramsB(), iterations);
    double speedup = (a.timing.avgMs - b.timing.avgMs) / a.timing.avgMs * 100.0;
    return {a, b, speedup};
  }

  static void printRangeUnifiedRow(const string& label, const RangeBenchmarkSetup& setup) {
    if constexpr (ENABLE_COMPARISON) {
      printComparisonRow(label, runRangeComparison(setup));
    } else {
      printRow(label, runRangeBenchmarkWithParams(setup, paramsA()).timing);
    }
  }

  // Debug: run single comparison with detailed output
  struct DetailedResult {
    vector<Result> results;
    SearchStats stats;
  };

  static DetailedResult runDetailedWithParams(const BenchmarkSetup& cfg, OptimizerParams params,
                                               bool trackStates = false) {
    params.trackExploredStates = trackStates;
    MotionOptimizer opt(config);
    auto [results, stats] = opt.optimize(cfg.lines, cfg.firstPos, RunningEffort(), cfg.lastPos, "",
                     navContext, cfg.boundary, EXPLORABLE_MOTIONS, params);
    return {results, stats};
  }

  // Show which motion classes would be explored from a given position toward goal
  static void printMotionClasses(Position pos, Position goal, const Lines& lines) {
    cout << "  From (" << pos.line << "," << pos.col << ") -> (" << goal.line << "," << goal.col << "):\n";

    // Determine which classes the pruned version would use
    vector<string> classes;
    if (pos.line == goal.line) {
      if (pos.col > goal.col) {
        classes = {"Left (h,0,^)", "Backward-crossing (b,B,ge,gE,{,()"};
      } else {
        classes = {"Right (l,$,^)", "Forward-crossing (w,W,e,E,},))"};
      }
    } else {
      if (pos.line < goal.line) {
        classes.push_back("Down (j,<C-d>,G)");
        classes.push_back("Forward-crossing (w,W,e,E,},))");
      } else {
        classes.push_back("Up (k,<C-u>,gg)");
        classes.push_back("Backward-crossing (b,B,ge,gE,{,()");
      }
      if (pos.col > goal.col) {
        classes.push_back("Left (h,0,^)");
        if (pos.line < goal.line) classes.push_back("Backward-crossing");
      } else if (pos.col < goal.col) {
        classes.push_back("Right (l,$,^)");
        if (pos.line > goal.line) classes.push_back("Forward-crossing");
      }
    }

    cout << "    A (all): Left, Right, Up, Down, Forward-crossing, Backward-crossing\n";
    cout << "    B (pruned): ";
    for (size_t i = 0; i < classes.size(); i++) {
      if (i > 0) cout << ", ";
      cout << classes[i];
    }
    cout << "\n";

    // Show what's dropped
    vector<string> allClasses = {"Left", "Right", "Up", "Down", "Forward-crossing", "Backward-crossing"};
    vector<string> dropped;
    for (const auto& c : allClasses) {
      bool found = false;
      for (const auto& selected : classes) {
        if (selected.find(c) != string::npos) { found = true; break; }
      }
      if (!found) dropped.push_back(c);
    }
    if (!dropped.empty()) {
      cout << "    Dropped by B: ";
      for (size_t i = 0; i < dropped.size(); i++) {
        if (i > 0) cout << ", ";
        cout << dropped[i];
      }
      cout << "\n";
    }
  }

  static void printDetailedComparison(const BenchmarkSetup& cfg) {
    cout << "\n--- Detailed Comparison ---\n";
    cout << "Start: (" << cfg.firstPos.line << ", " << cfg.firstPos.col << ")\n";
    cout << "Goal:  (" << cfg.lastPos.line << ", " << cfg.lastPos.col << ")\n";
    cout << "Lines: " << cfg.lines.size() << "\n\n";

    // Show motion class selection for the start position
    cout << "Motion class selection:\n";
    printMotionClasses(cfg.firstPos, cfg.lastPos, cfg.lines);
    cout << "\n";

    auto resA = runDetailedWithParams(cfg, paramsA(), true);  // track states
    auto resB = runDetailedWithParams(cfg, paramsB(), true);  // track states

    auto printResults = [](const DetailedResult& res, const char* name) {
      cout << name << ": nodes=" << res.stats.nodesExplored
           << " motions=" << res.stats.motionsEmitted
           << " skipped=" << res.stats.statesSkipped
           << " avg=" << fixed << setprecision(1) << res.stats.avgMotionsPerState() << "/state\n";
      cout << "   Results (" << res.results.size() << "):\n";
      for (size_t i = 0; i < min(res.results.size(), size_t(5)); i++) {
        string seqStr;
        for (const auto& seq : res.results[i].sequences) {
          seqStr += seq.keys;
        }
        cout << "     \"" << seqStr << "\" (cost=" << setprecision(2) << res.results[i].keyCost << ")\n";
      }
    };

    printResults(resA, "A");
    cout << "\n";
    printResults(resB, "B");

    // Compare explored states
    if (!resA.stats.exploredStates.empty() && !resB.stats.exploredStates.empty()) {
      // Build sets of (line,col) for comparison
      set<pair<int,int>> posA, posB;
      map<pair<int,int>, string> seqA, seqB;  // position -> first sequence that reached it

      for (const auto& s : resA.stats.exploredStates) {
        auto key = make_pair(s.line, s.col);
        if (posA.insert(key).second) seqA[key] = s.sequence;
      }
      for (const auto& s : resB.stats.exploredStates) {
        auto key = make_pair(s.line, s.col);
        if (posB.insert(key).second) seqB[key] = s.sequence;
      }

      // Find differences
      vector<pair<int,int>> onlyInA, onlyInB;
      for (const auto& p : posA) if (posB.find(p) == posB.end()) onlyInA.push_back(p);
      for (const auto& p : posB) if (posA.find(p) == posA.end()) onlyInB.push_back(p);

      cout << "\nState differences:\n";
      cout << "  Positions explored: A=" << posA.size() << ", B=" << posB.size()
           << ", shared=" << (posA.size() - onlyInA.size()) << "\n";

      if (!onlyInA.empty()) {
        cout << "  Only in A (up to 5):\n";
        for (size_t i = 0; i < min(onlyInA.size(), size_t(5)); i++) {
          auto [l,c] = onlyInA[i];
          cout << "    (" << l << "," << c << ") via \"" << seqA[{l,c}] << "\"\n";
        }
      }
      if (!onlyInB.empty()) {
        cout << "  Only in B (up to 5):\n";
        for (size_t i = 0; i < min(onlyInB.size(), size_t(5)); i++) {
          auto [l,c] = onlyInB[i];
          cout << "    (" << l << "," << c << ") via \"" << seqB[{l,c}] << "\"\n";
        }
      }
    }
    cout << "---\n";
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

  // ================== Comparison Output ==================
  static void printComparisonHeader(const string& paramName) {
    cout << setw(12) << left << paramName
         << "| " << setw(8) << right << "A (ms)"
         << " | " << setw(6) << "Nodes"
         << " | " << setw(7) << "Motions"
         << " | " << setw(8) << "B (ms)"
         << " | " << setw(6) << "Nodes"
         << " | " << setw(7) << "Motions"
         << " | " << setw(8) << "Speedup"
         << endl;
    cout << string(12, '-')
         << "|" << string(10, '-') << "|" << string(8, '-') << "|" << string(9, '-')
         << "|" << string(10, '-') << "|" << string(8, '-') << "|" << string(9, '-')
         << "|" << string(10, '-') << endl;
  }

  static void printComparisonRow(const string& label, const ComparisonResult& cmp) {
    cout << setw(12) << left << label
         << "| " << setw(8) << right << fixed << setprecision(2) << cmp.a.timing.avgMs
         << " | " << setw(6) << cmp.a.search.nodesExplored
         << " | " << setw(7) << cmp.a.search.motionsEmitted
         << " | " << setw(8) << cmp.b.timing.avgMs
         << " | " << setw(6) << cmp.b.search.nodesExplored
         << " | " << setw(7) << cmp.b.search.motionsEmitted
         << " | " << setw(7) << setprecision(1) << cmp.speedupPercent << "%"
         << endl;
  }

  // Unified entry points that check ENABLE_COMPARISON
  static void printUnifiedHeader(const string& paramName) {
    if constexpr (ENABLE_COMPARISON) {
      printComparisonHeader(paramName);
    } else {
      printTableHeader(paramName);
    }
  }

  static void printUnifiedRow(const string& label, const BenchmarkSetup& setup) {
    if constexpr (ENABLE_COMPARISON) {
      printComparisonRow(label, runComparison(setup));
    } else {
      printRow(label, runBenchmark(setup).timing);
    }
  }

  // With custom params override (for SearchDepth test)
  static void printUnifiedRowWithParams(const string& label, const BenchmarkSetup& setup,
                                         const OptimizerParams& baseParams) {
    if constexpr (ENABLE_COMPARISON) {
      // Merge baseParams into A and B
      OptimizerParams pA = paramsA();
      OptimizerParams pB = paramsB();
      pA.maxResults = baseParams.maxResults;
      pA.maxNodesExplored = baseParams.maxNodesExplored;
      pA.exploreFactor = baseParams.exploreFactor;
      pB.maxResults = baseParams.maxResults;
      pB.maxNodesExplored = baseParams.maxNodesExplored;
      pB.exploreFactor = baseParams.exploreFactor;

      BenchmarkResult a = runBenchmarkWithParams(setup, pA);
      BenchmarkResult b = runBenchmarkWithParams(setup, pB);
      double speedup = (a.timing.avgMs - b.timing.avgMs) / a.timing.avgMs * 100.0;
      printComparisonRow(label, {a, b, speedup});
    } else {
      OptimizerParams p = paramsA();
      p.maxResults = baseParams.maxResults;
      p.maxNodesExplored = baseParams.maxNodesExplored;
      p.exploreFactor = baseParams.exploreFactor;
      printRowWithStats(label, runBenchmarkWithParams(setup, p));
    }
  }
};

Config MotionOptimizerBench::config = Config::uniform();
NavContext MotionOptimizerBench::navContext;


TEST_F(MotionOptimizerBench, BufferSize) {
  printBanner("MotionOptimizer Benchmark Suite");
  if constexpr (ENABLE_COMPARISON) {
    cout << "Comparison: " << COMPARISON_NAME << endl;
  }
  printHeader("Buffer Size Benchmark");
  printUnifiedHeader("Lines");

  vector<int> lineCounts = {1, 5, 10, 15, 20, 30};

  for (int numLines : lineCounts) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 30, BufferShape::Uniform);
    printUnifiedRow(to_string(numLines), BenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, LineLength) {
  printHeader("Line Length Benchmark");
  printUnifiedHeader("Chars");

  vector<int> lineLengths = {10, 20, 40, 60, 80};
  for (int avgLen : lineLengths) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(20, avgLen, BufferShape::Uniform);
    printUnifiedRow(to_string(avgLen), BenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, BufferShape) {
  printHeader("Buffer Shape Benchmark");
  printUnifiedHeader("Shape");

  vector<pair<string, BufferShape>> shapes = {
      {"Uniform", BufferShape::Uniform},
      {"CodeLike", BufferShape::CodeLike},
  };

  for (const auto& [name, shape] : shapes) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(20, 30, shape);
    printUnifiedRow(name, BenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, BoundarySettings) {
  printHeader("Boundary Settings Benchmark");
  printUnifiedHeader("Boundary");

  RandomGen::seed(42);
  Lines lines = generateBuffer(20, 30, BufferShape::Uniform);
  // todo
}

// =============================================================================
// Benchmark: Range Optimization
// =============================================================================

TEST_F(MotionOptimizerBench, RangePruning) {
  printHeader("Range Optimization Benchmark (optimizeToRange)");
  printUnifiedHeader("Lines");

  vector<int> lineCounts = {5, 10, 20, 30, 40};

  for (int numLines : lineCounts) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 30, BufferShape::Uniform);
    printRangeUnifiedRow(to_string(numLines), RangeBenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, RangePruningCodeLike) {
  printHeader("Range Optimization (CodeLike buffers)");
  printUnifiedHeader("Lines");

  vector<int> lineCounts = {10, 20, 40};

  for (int numLines : lineCounts) {
    RandomGen::seed(42);
    Lines lines = generateBuffer(numLines, 30, BufferShape::CodeLike);
    printRangeUnifiedRow(to_string(numLines), RangeBenchmarkSetup(lines));
  }
}

TEST_F(MotionOptimizerBench, RangeSize) {
  printHeader("Range Size Benchmark (fixed 20-line buffer)");
  printUnifiedHeader("Range");

  RandomGen::seed(42);
  Lines lines = generateBuffer(20, 40, BufferShape::Uniform);

  // Test various range sizes: (chars, lines)
  vector<tuple<string, int, int>> rangeSizes = {
      {"1 col",      1,  1},   // Single character
      {"6 cols",     6,  1},   // Default: 6 chars same line
      {"15 cols",    15, 1},   // Larger same-line range
      {"30 cols",    30, 1},   // Most of a line
      {"40 (2 ln)",  40, 2},   // Multi-line range (~40 chars across 2 lines)
  };

  for (const auto& [label, chars, numLines] : rangeSizes) {
    printRangeUnifiedRow(label, RangeBenchmarkSetup(lines, chars, numLines));
  }
}

// =============================================================================
// Benchmark: Search Depth
// =============================================================================

TEST_F(MotionOptimizerBench, SearchDepth) {
  printHeader("Search Depth Benchmark");
  if constexpr (ENABLE_COMPARISON) {
    printComparisonHeader("Depth");
  } else {
    printTableHeaderWithStats("Depth");
  }

  // Larger buffer + more results + higher exploreFactor so that
  // node limit (depth) becomes the limiting factor, not maxResults
  RandomGen::seed(42);
  Lines lines = generateBuffer(40, 30, BufferShape::CodeLike);
  BenchmarkSetup setup(lines);

  for (int depth : {1000, 5000, 10000, 50000, 100000}) {
    OptimizerParams baseParams = {.maxNodesExplored = depth, .exploreFactor = 3.0};
    printUnifiedRowWithParams(to_string(depth), setup, baseParams);
  }
}

// =============================================================================
// Debug: Detailed single-case comparison
// =============================================================================
// Enable this test to see detailed output for a specific scenario.
// Useful for understanding why A/B differ in node counts.

TEST_F(MotionOptimizerBench, DISABLED_DetailedComparison) {
  printHeader("Detailed Single-Case Comparison");

  // Case 1: Standard case
  cout << "\n=== Case 1: Standard 20-line buffer ===\n";
  RandomGen::seed(42);
  Lines lines1 = generateBuffer(20, 30, BufferShape::Uniform);
  BenchmarkSetup setup1(lines1);
  printDetailedComparison(setup1);

  // Case 2: CodeLike buffer (from SearchDepth where B had more nodes)
  cout << "\n=== Case 2: CodeLike 40-line buffer ===\n";
  RandomGen::seed(42);
  Lines lines2 = generateBuffer(40, 30, BufferShape::CodeLike);
  BenchmarkSetup setup2(lines2);
  printDetailedComparison(setup2);
}
