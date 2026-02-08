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

constexpr bool ENABLE_COMPARISON = true;
constexpr const char* COMPARISON_NAME = "Search Mode (A = Eager, B = Lazy)";

// NOTE: Must be static, not inline - inline causes incorrect behavior with templates
static EditOptimizerParams paramsA() {
  EditOptimizerParams p;
  // Eager: check goals during generation (default)
  return p;
}

static EditOptimizerParams paramsB() {
  EditOptimizerParams p;
  // Lazy: push goal states through PQ, record on pop
  p.searchMode = SearchMode::Lazy;
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
                                       int iterations = DEFAULT_TIMING_ITERATIONS) {
    EditOptimizer opt(config);
    SearchStats lastStats;

    bool isPureDeletion = (cfg.goalLines.size() == 1 && cfg.goalLines[0].empty());

    TimingStats timing = measureTiming(
        [&]() {
          EditResult result = isPureDeletion
              ? opt.optimizePureDeletion(cfg.initialLines, cfg.boundary, params)
              : params.useSuffixCache
                  ? opt.optimizeEditWithSuffixCache(cfg.initialLines, cfg.goalLines, cfg.boundary, params)
                  : opt.optimizeEdit(cfg.initialLines, cfg.goalLines, cfg.boundary, params);
          lastStats = result.stats;
        },
        iterations);

    return {timing, lastStats};
  }

  static int maxResultsFor(const BenchmarkSetup& setup) {
    return max(10, setup.initialLines.totalPositions() / 4);
  }

  // Apply maxResults cap based on the setup's totalPositions
  static EditOptimizerParams withCap(EditOptimizerParams p, const BenchmarkSetup& s) {
    return p.withMaxResults(max(10, s.initialLines.totalPositions() / 4));
  }

  // ================== Print Helpers (Multi-Seed) ==================
  static void printMultiSeedRow(const string& label, int numLines, int avgLen = 30,
                                BufferShape shape = BufferShape::CodeLike) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, EditOptimizerParams, BenchmarkSetup>(
        label,
        [=]() { return BenchmarkSetup(generateBuffer(numLines, avgLen, shape)); },
        paramsA(), paramsB(),
        [](const BenchmarkSetup& s, const EditOptimizerParams& p) {
          return runWithParams(s, withCap(p, s));
        });
  }

  static void printMultiSeedRowWithNodes(const string& label, int numLines, int avgLen,
                                         int maxNodes) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, EditOptimizerParams, BenchmarkSetup>(
        label,
        [=]() { return BenchmarkSetup(generateBuffer(numLines, avgLen)); },
        paramsA().withMaxNodesExplored(maxNodes),
        paramsB().withMaxNodesExplored(maxNodes),
        [](const BenchmarkSetup& s, const EditOptimizerParams& p) {
          return runWithParams(s, withCap(p, s));
        });
  }

  // ================== Print Helpers (Single Setup) ==================
  static void printRow(const string& label, const BenchmarkSetup& setup) {
    int mr = maxResultsFor(setup);
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup,
        paramsA().withMaxResults(mr), paramsB().withMaxResults(mr),
        [](const BenchmarkSetup& s, const EditOptimizerParams& p) {
          return runWithParams(s, p);
        });
  }

  static void printRowWithNodes(const string& label, const BenchmarkSetup& setup, int maxNodes) {
    int mr = maxResultsFor(setup);
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup,
        paramsA().withMaxNodesExplored(maxNodes).withMaxResults(mr),
        paramsB().withMaxNodesExplored(maxNodes).withMaxResults(mr),
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
  cout << "(Averaged across " << DEFAULT_SEED_COUNT << " seeds - "
       << getSeedModeDescription() << ")\n";
  printHeader("Buffer Size Benchmark (Pure Deletion)");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {1, 3, 5, 10, 15}) {
    printMultiSeedRow(to_string(numLines), numLines, 30);
  }
}

TEST_F(EditOptimizerBench, LineLength) {
  printHeader("Line Length Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Chars");

  for (int avgLen : {10, 20, 40, 60}) {
    printMultiSeedRow(to_string(avgLen), 5, avgLen);
  }
}

TEST_F(EditOptimizerBench, BufferShape) {
  printHeader("Buffer Shape Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Shape");

  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    printMultiSeedRow(name, 10, 30, shape);
  }
}

TEST_F(EditOptimizerBench, SearchDepth) {
  printHeader("Search Depth Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Depth");

  for (int depth : {1000, 5000, 10000, 50000}) {
    printMultiSeedRowWithNodes(to_string(depth), 15, 30, depth);
  }
}

TEST_F(EditOptimizerBench, MultiLineDelete) {
  printHeader("Multi-Line Deletion Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Lines");

  for (int numLines : {2, 4, 6, 8, 10}) {
    printMultiSeedRow(to_string(numLines), numLines, 20);
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

TEST_F(EditOptimizerBench, MultiLineEdit) {
  printHeader("Multi-Line Edit (Replacement) Benchmark");
  printUnifiedHeader<ENABLE_COMPARISON>("Case");

  // Multi-line deletion replaced with single-line insertion
  // This is the case where J-based paths matter most
  auto makeMultiLineEditSetup = [](const Lines& buffer, const Lines& goal,
                                    Position editBegin, Position editEnd) {
    Lines editRegion = buffer.getSpan(editBegin, editEnd);
    EditBoundary boundary(buffer, editBegin, editEnd);
    return BenchmarkSetup(editRegion, goal, boundary);
  };

  // Case 1: Realistic - 2 lines with prefix, replace with 1 word
  {
    Lines buffer = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
    Lines goal = {"Florida"};
    Position editBegin(0, 23);
    Position editEnd(1, 19);
    printRow("2L->1w", makeMultiLineEditSetup(buffer, goal, editBegin, editEnd));
  }

  // Case 2: 3 lines replaced with short text
  {
    Lines buffer = {"The quick brown fox jumps over the lazy dog",
                    "and then runs around the park",
                    "before going home to sleep"};
    Lines goal = {"walked away"};
    Position editBegin(0, 20);
    Position editEnd(2, 26);
    printRow("3L->1w", makeMultiLineEditSetup(buffer, goal, editBegin, editEnd));
  }

  // Case 3: 5 lines with prefix+suffix
  {
    Lines buffer = {"prefix stuff delete me line one",
                    "delete me line two",
                    "delete me line three",
                    "delete me line four",
                    "delete me line five and suffix here"};
    Lines goal = {"replaced"};
    Position editBegin(0, 13);
    Position editEnd(4, 22);
    printRow("5L+bnd", makeMultiLineEditSetup(buffer, goal, editBegin, editEnd));
  }

  // Case 4: Random multi-line edits (averaged across seeds)
  for (int numLines : {2, 4, 6}) {
    runMultiSeedBenchmark<ENABLE_COMPARISON, EditOptimizerParams, BenchmarkSetup>(
        to_string(numLines) + "L rand",
        [=]() {
          Lines buffer = generateBuffer(numLines, 25);
          // Replace all content with a short word
          Lines goal = {"replacement"};
          EditBoundary boundary(buffer, Position(0, 0), buffer.endPos());
          return BenchmarkSetup(buffer, goal, boundary);
        },
        paramsA(), paramsB(),
        [](const BenchmarkSetup& s, const EditOptimizerParams& p) {
          return runWithParams(s, withCap(p, s));
        });
  }
}
