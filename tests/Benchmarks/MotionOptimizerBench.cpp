// tests/Benchmarks/MotionOptimizerBench.cpp
//
// Performance benchmarks for MotionOptimizer using Google Benchmark.
//
// Run: ./build/tests/vimficiency_benchmarks --benchmark_filter="MotionOptimizer.*"

#include <algorithm>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionOptimizerParams.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

// =============================================================================
// A/B Comparison Configuration
// =============================================================================

constexpr bool ENABLE_COMPARISON = false;

static MotionOptimizerParams motionParamsA() {
  return MotionOptimizerParams{};
}

static MotionOptimizerParams motionParamsB() {
  return MotionOptimizerParams{}.withDirectionalPruning(false);
}

static MotionOptimizerRangeParams rangeParamsA() {
  return MotionOptimizerRangeParams{};
}

static MotionOptimizerRangeParams rangeParamsB() {
  return MotionOptimizerRangeParams{}.withDirectionalPruning(false);
}

// =============================================================================
// Benchmark Infrastructure
// =============================================================================

static Config benchConfig = Config::uniform();

struct BenchmarkSetup {
  Lines lines;
  MotionBoundary boundary{};
  Position firstPos;
  Position lastPos;

  BenchmarkSetup(const Lines& lines) : lines(lines) {
    firstPos = randomFirstPos(this->lines);
    lastPos = randomLastPos(this->lines);
    Position boundaryEnd(lastPos.line, lastPos.col + 1);
    boundary = MotionBoundary(this->lines, firstPos, boundaryEnd, true, true);
  }
};

struct RangeBenchmarkSetup {
  Lines lines;
  MotionBoundary boundary{};
  Position initialPos;
  Position rangeFirst;
  Position rangeEnd;

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

static void runWithParams(const BenchmarkSetup& cfg,
                          const MotionOptimizerParams& params,
                          SearchStats& outStats) {
  MotionOptimizer opt(benchConfig);
  auto result = opt.optimize(cfg.lines, cfg.firstPos, cfg.lastPos,
                             params, "", cfg.boundary);
  outStats = result.stats;
}

static void runRangeWithParams(const RangeBenchmarkSetup& cfg,
                               const MotionOptimizerRangeParams& params,
                               SearchStats& outStats) {
  MotionOptimizer opt(benchConfig);
  auto result = opt.optimizeToRange(
      cfg.lines, cfg.initialPos, cfg.rangeFirst, cfg.rangeEnd,
      params, "", cfg.boundary);
  outStats = result.stats;
}

// =============================================================================
// Benchmark Functions
// =============================================================================

static void BM_MotionBufferSize(benchmark::State& state, bool useB) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(numLines, 30));
    auto params = useB ? motionParamsB() : motionParamsA();
    runWithParams(setup, params, lastStats);
    iter++;
  }
  setSearchCounters(state, lastStats);
}

static void BM_MotionLineLength(benchmark::State& state, bool useB) {
  int avgLen = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(20, avgLen));
    auto params = useB ? motionParamsB() : motionParamsA();
    runWithParams(setup, params, lastStats);
    iter++;
  }
  setSearchCounters(state, lastStats);
}

static void BM_MotionBufferShape(benchmark::State& state, bool useB, BufferShape shape) {
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(20, 30, shape));
    auto params = useB ? motionParamsB() : motionParamsA();
    runWithParams(setup, params, lastStats);
    iter++;
  }
  setSearchCounters(state, lastStats);
}

static void BM_MotionRangeBufferLines(benchmark::State& state, bool useB) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = RangeBenchmarkSetup(generateBuffer(numLines, 30));
    auto params = useB ? rangeParamsB() : rangeParamsA();
    runRangeWithParams(setup, params, lastStats);
    iter++;
  }
  setSearchCounters(state, lastStats);
}

static void BM_MotionRangeResultSize(benchmark::State& state, bool useB,
                                      int rangeChars, int rangeLines) {
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = RangeBenchmarkSetup(generateBuffer(20, 30), rangeChars, rangeLines);
    auto params = useB ? rangeParamsB() : rangeParamsA();
    runRangeWithParams(setup, params, lastStats);
    iter++;
  }
  setSearchCounters(state, lastStats);
}

// =============================================================================
// Registration
// =============================================================================

static void registerArgBenchmark(const string& name, void(*fn)(benchmark::State&, bool),
                                 const vector<int>& args) {
  auto* a = benchmark::RegisterBenchmark(name + "/Standard", fn, false);
  for (int v : args) a->Arg(v);
  a->Iterations(DEFAULT_SEED_COUNT);

  if (ENABLE_COMPARISON) {
    auto* b = benchmark::RegisterBenchmark(name + "/NoPruning", fn, true);
    for (int v : args) b->Arg(v);
    b->Iterations(DEFAULT_SEED_COUNT);
  }
}

static int registerMotionBenchmarks = []() {
  // BufferSize
  registerArgBenchmark("MotionOptimizer/BufferSize", BM_MotionBufferSize,
                       {1, 5, 10, 15, 20, 30});

  // LineLength
  registerArgBenchmark("MotionOptimizer/LineLength", BM_MotionLineLength,
                       {10, 20, 40, 60, 80});

  // BufferShape
  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    auto* a = benchmark::RegisterBenchmark(
        "MotionOptimizer/BufferShape/" + name + "/Standard", BM_MotionBufferShape, false, shape);
    a->Iterations(DEFAULT_SEED_COUNT);
    if (ENABLE_COMPARISON) {
      auto* b = benchmark::RegisterBenchmark(
          "MotionOptimizer/BufferShape/" + name + "/NoPruning", BM_MotionBufferShape, true, shape);
      b->Iterations(DEFAULT_SEED_COUNT);
    }
  }

  // RangeBufferLines
  registerArgBenchmark("MotionOptimizer/RangeBufferLines", BM_MotionRangeBufferLines,
                       {5, 10, 20, 30, 40});

  // RangeResultSize
  for (const auto& [label, chars, rangeLines] : vector<tuple<string, int, int>>{
           {"1col", 1, 1},
           {"3cols", 3, 1},
           {"6cols", 6, 1},
           {"10cols", 10, 1},
           {"30_2ln", 30, 2}}) {
    auto* a = benchmark::RegisterBenchmark(
        "MotionOptimizer/RangeResultSize/" + label + "/Standard",
        BM_MotionRangeResultSize, false, chars, rangeLines);
    a->Iterations(DEFAULT_SEED_COUNT);
    if (ENABLE_COMPARISON) {
      auto* b = benchmark::RegisterBenchmark(
          "MotionOptimizer/RangeResultSize/" + label + "/NoPruning",
          BM_MotionRangeResultSize, true, chars, rangeLines);
      b->Iterations(DEFAULT_SEED_COUNT);
    }
  }

  return 0;
}();

} // namespace
