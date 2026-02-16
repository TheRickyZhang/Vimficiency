// tests/Benchmarks/EditOptimizerBench.cpp
//
// Performance benchmarks for EditOptimizer using Google Benchmark.
//
// Run: ./build/tests/vimficiency_benchmarks --benchmark_filter="EditOptimizer.*"

#include <algorithm>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/EditBoundary.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

// =============================================================================
// Benchmark Infrastructure
// =============================================================================

static Config benchConfig = Config::uniform();

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

static void runBenchmark(const BenchmarkSetup& cfg,
                         const EditOptimizerParams& params,
                         SearchStats& outStats) {
  EditOptimizer opt(benchConfig);
  EditResult result = opt.optimizeEdit(cfg.initialLines, cfg.goalLines, cfg.boundary, params);
  accumulateStats(outStats, result.stats);
}

static EditOptimizerParams withCap(EditOptimizerParams p, const BenchmarkSetup& s) {
  return p.withMaxResults(max(10, s.initialLines.totalPositions() / 4));
}

// =============================================================================
// Benchmark Functions
// =============================================================================

static void BM_EditBufferSize(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(numLines, 30));
    runBenchmark(setup, withCap({}, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditLineLength(benchmark::State& state) {
  int avgLen = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(5, avgLen));
    runBenchmark(setup, withCap({}, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditBufferShape(benchmark::State& state, BufferShape shape) {
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(10, 30, shape));
    runBenchmark(setup, withCap({}, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditSearchDepth(benchmark::State& state) {
  int maxNodes = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(15, 30));
    EditOptimizerParams params;
    params.maxNodesExplored = maxNodes;
    runBenchmark(setup, withCap(params, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditMultiLineDelete(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(numLines, 20));
    runBenchmark(setup, withCap({}, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditBoundaryConstraints(benchmark::State& state, int boundaryType) {
  SearchStats totalStats;
  for (auto _ : state) {
    RandomGen::seed(42);
    Lines fullBuffer = generateBuffer(10, 30);

    BenchmarkSetup setup(fullBuffer);
    switch (boundaryType) {
      case 0: // None
        break;
      case 1: { // Prefix
        Position firstPos(0, 15);
        Position endPos = fullBuffer.endPos();
        Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
        EditBoundary boundary(fullBuffer, firstPos, endPos);
        setup = BenchmarkSetup(editRegion, {""}, boundary);
        break;
      }
      case 2: { // Suffix
        Position firstPos(0, 0);
        int lastLine = fullBuffer.lastLine();
        int midCol = static_cast<int>(fullBuffer[lastLine].size()) / 2;
        Position endPos(lastLine, midCol + 1);
        Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
        EditBoundary boundary(fullBuffer, firstPos, endPos);
        setup = BenchmarkSetup(editRegion, {""}, boundary);
        break;
      }
      case 3: { // Both
        Position firstPos(0, 10);
        int lastLine = fullBuffer.lastLine();
        int lastLineLen = static_cast<int>(fullBuffer[lastLine].size());
        Position endPos(lastLine, max(1, lastLineLen - 10 + 1));
        Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
        EditBoundary boundary(fullBuffer, firstPos, endPos);
        setup = BenchmarkSetup(editRegion, {""}, boundary);
        break;
      }
    }
    int mr = max(10, setup.initialLines.totalPositions() / 4);
    runBenchmark(setup, EditOptimizerParams{}.withMaxResults(mr), totalStats);
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditMultiLineFixed(benchmark::State& state, int caseNum) {
  SearchStats totalStats;
  for (auto _ : state) {
    BenchmarkSetup setup({""});

    auto makeSetup = [](const Lines& buffer, const Lines& goal,
                        Position editBegin, Position editEnd) {
      Lines editRegion = buffer.getSpan(editBegin, editEnd);
      EditBoundary boundary(buffer, editBegin, editEnd);
      return BenchmarkSetup(editRegion, goal, boundary);
    };

    switch (caseNum) {
      case 0: { // 2L->1w
        Lines buffer = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
        Lines goal = {"Florida"};
        setup = makeSetup(buffer, goal, Position(0, 23), Position(1, 19));
        break;
      }
      case 1: { // 3L->1w
        Lines buffer = {"The quick brown fox jumps over the lazy dog",
                        "and then runs around the park",
                        "before going home to sleep"};
        Lines goal = {"walked away"};
        setup = makeSetup(buffer, goal, Position(0, 20), Position(2, 26));
        break;
      }
      case 2: { // 5L+bnd
        Lines buffer = {"prefix stuff delete me line one",
                        "delete me line two",
                        "delete me line three",
                        "delete me line four",
                        "delete me line five and suffix here"};
        Lines goal = {"replaced"};
        setup = makeSetup(buffer, goal, Position(0, 13), Position(4, 22));
        break;
      }
    }
    int mr = max(10, setup.initialLines.totalPositions() / 4);
    runBenchmark(setup, EditOptimizerParams{}.withMaxResults(mr), totalStats);
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditMultiLineRandom(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines buffer = generateBuffer(numLines, 25);
    Lines goal = {"replacement"};
    EditBoundary boundary(buffer, Position(0, 0), buffer.endPos());
    auto setup = BenchmarkSetup(buffer, goal, boundary);
    runBenchmark(setup, withCap({}, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// Small full-content change: matches MultiLine_FullBufferChange pattern
// (2-3 lines × 4-8 chars, initial → different goal, default params)
static void BM_EditSmallChange(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines source = randomLines(numLines, 4, 8);
    Lines goal = randomLines(numLines, 4, 8);
    if (source == goal) goal[0] = "changed";
    EditBoundary boundary(source, {0, 0}, source.endPos());
    auto setup = BenchmarkSetup(source, goal, boundary);
    runBenchmark(setup, {}, totalStats);  // default maxResults, no cap
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// Small embedded change: matches MultiLine_EmbeddedChange pattern
// (edit region with prefix/suffix boundary, small lines)
static void BM_EditSmallEmbeddedChange(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    // Generate full buffer, carve out edit region with prefix/suffix
    Lines fullBuffer = randomLines(numLines + 1, 8, 15);
    int prefixLen = min(4, static_cast<int>(fullBuffer[0].size()) / 2);
    int lastLine = static_cast<int>(fullBuffer.size()) - 1;
    int suffixLen = min(4, static_cast<int>(fullBuffer[lastLine].size()) / 2);
    Position firstPos(0, prefixLen);
    Position endPos(lastLine, static_cast<int>(fullBuffer[lastLine].size()) - suffixLen);
    if (endPos.col <= 0) endPos.col = static_cast<int>(fullBuffer[lastLine].size());
    Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
    EditBoundary boundary(fullBuffer, firstPos, endPos);
    Lines goal = randomLines(static_cast<int>(editRegion.size()), 4, 8);
    auto setup = BenchmarkSetup(editRegion, goal, boundary);
    runBenchmark(setup, {}, totalStats);  // default maxResults, no cap
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// =============================================================================
// Registration
// =============================================================================

// Helper to register parameterized (Arg) benchmarks
static void registerArgBenchmark(const string& name, void(*fn)(benchmark::State&),
                                 const vector<int>& args) {
  auto* b = benchmark::RegisterBenchmark(name, fn);
  for (int v : args) b->Arg(v);
  b->Iterations(DEFAULT_SEED_COUNT);
}

// Static initialization block to register all benchmarks
static int registerEditBenchmarks = []() {
  // BufferSize
  registerArgBenchmark("EditOpt/BufferSize", BM_EditBufferSize,
                       {1, 3, 5, 10, 15});

  // LineLength
  registerArgBenchmark("EditOpt/LineLength", BM_EditLineLength,
                       {10, 20, 40, 60});

  // BufferShape
  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    auto* b = benchmark::RegisterBenchmark(
        "EditOpt/BufferShape/" + name, BM_EditBufferShape, shape);
    b->Iterations(DEFAULT_SEED_COUNT);
  }

  // SearchDepth
  registerArgBenchmark("EditOpt/SearchDepth", BM_EditSearchDepth,
                       {1000, 5000, 10000, 50000});

  // MultiLineDelete
  registerArgBenchmark("EditOpt/MultiLineDelete", BM_EditMultiLineDelete,
                       {2, 4, 6, 8, 10});

  // BoundaryConstraints
  for (const auto& [name, type] : vector<pair<string, int>>{
           {"None", 0}, {"Prefix", 1}, {"Suffix", 2}, {"Both", 3}}) {
    benchmark::RegisterBenchmark("EditOpt/Boundary/" + name, BM_EditBoundaryConstraints, type);
  }

  // MultiLineEdit - fixed cases
  for (const auto& [name, caseNum] : vector<pair<string, int>>{
           {"2L->1w", 0}, {"3L->1w", 1}, {"5L+bnd", 2}}) {
    benchmark::RegisterBenchmark("EditOpt/MultiLineEdit/" + name, BM_EditMultiLineFixed, caseNum);
  }

  // MultiLineEdit - random
  registerArgBenchmark("EditOpt/MultiLineEdit/Random", BM_EditMultiLineRandom,
                       {2, 4, 6});

  // SmallChange - matches correctness test pattern (small region, full content change)
  registerArgBenchmark("EditOpt/SmallChange", BM_EditSmallChange,
                       {1, 2, 3});

  // SmallEmbeddedChange - small edit region with prefix/suffix boundary
  registerArgBenchmark("EditOpt/SmallEmbeddedChange", BM_EditSmallEmbeddedChange,
                       {1, 2, 3});

  return 0;
}();

} // namespace
