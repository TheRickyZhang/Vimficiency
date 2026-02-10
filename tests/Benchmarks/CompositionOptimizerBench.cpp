// tests/Benchmarks/CompositionOptimizerBench.cpp
//
// Performance benchmarks for CompositionOptimizer using Google Benchmark.
// Covers patterns from correctness tests: small multi-diff edits on 2-3 line buffers.
//
// Run: ./build/tests/vimficiency_benchmarks --benchmark_filter="CompositionOptimizer.*"

#include <algorithm>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Optimizer/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Utils/RandomGeneration.h"

using namespace std;

static Config benchConfig = Config::uniform();

// =============================================================================
// Benchmark Functions
// =============================================================================

// Two edits on different lines: matches TwoEdits_DifferentLines pattern
// (3 lines, edit line 0 and line 2, keep middle unchanged)
static void BM_CompTwoEditsDiffLines(benchmark::State& state) {
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines initial = randomLines(3, 5, 10);
    Lines goal = initial;
    goal[0] = randomWord(RandomGen::range(4, 8));
    goal[2] = randomWord(RandomGen::range(4, 8));

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    lastStats = result.stats;
    iter++;
  }
  setSearchCounters(state, lastStats);
}

// Two edits on same line: matches TwoEdits_SameLine pattern
static void BM_CompTwoEditsSameLine(benchmark::State& state) {
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    string line = randomWord(RandomGen::range(8, 15));
    Lines initial = {line};
    // Change first 3 and last 3 chars
    string goal = line;
    for (int i = 0; i < min(3, static_cast<int>(goal.size())); i++)
      goal[i] = 'a' + RandomGen::range(0, 25);
    for (int i = max(0, static_cast<int>(goal.size()) - 3); i < static_cast<int>(goal.size()); i++)
      goal[i] = 'a' + RandomGen::range(0, 25);
    if (goal == line) goal[0] = (line[0] == 'z') ? 'a' : 'z';

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, {goal}, {0, 0});
    lastStats = result.stats;
    iter++;
  }
  setSearchCounters(state, lastStats);
}

// Single line substitution: matches SingleLine_Substitution pattern
static void BM_CompSingleLineSub(benchmark::State& state) {
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines initial = randomLines(1, 10, 20);
    Lines goal = randomLines(1, 10, 20);
    if (initial == goal) goal[0] = "different";

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    lastStats = result.stats;
    iter++;
  }
  setSearchCounters(state, lastStats);
}

// Multi-line single edit: matches MultiLine_SingleEdit pattern
static void BM_CompMultiLineSingleEdit(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines initial = randomLines(numLines, 5, 10);
    Lines goal = initial;
    // Change one line
    int editLine = RandomGen::range(0, numLines - 1);
    goal[editLine] = randomWord(RandomGen::range(4, 8));

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    lastStats = result.stats;
    iter++;
  }
  setSearchCounters(state, lastStats);
}

// Multi-line multi-edit: 3 edits spread across a buffer of varying size.
// Fixed edit count isolates the effect of buffer size (longer motions between diffs).
static void BM_CompMultiLineMultiEdit(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  constexpr int NUM_EDITS = 3;
  auto& seedMgr = SeedManager::instance();
  SearchStats lastStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines initial = randomLines(numLines, 8, 15);
    Lines goal = initial;
    // Spread edits evenly: near start, middle, and end
    int editLines[NUM_EDITS] = {
      0,
      numLines / 2,
      numLines - 1
    };
    for (int line : editLines) {
      goal[line] = randomWord(RandomGen::range(4, 12));
      if (goal[line] == initial[line]) goal[line] = "changed";
    }

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    lastStats = result.stats;
    iter++;
  }
  setSearchCounters(state, lastStats);
}

// =============================================================================
// Registration
// =============================================================================

static int registerCompositionBenchmarks = []() {
  auto* a = benchmark::RegisterBenchmark(
      "CompositionOptimizer/TwoEdits_DiffLines", BM_CompTwoEditsDiffLines);
  a->Iterations(DEFAULT_SEED_COUNT);

  auto* b = benchmark::RegisterBenchmark(
      "CompositionOptimizer/TwoEdits_SameLine", BM_CompTwoEditsSameLine);
  b->Iterations(DEFAULT_SEED_COUNT);

  auto* c = benchmark::RegisterBenchmark(
      "CompositionOptimizer/SingleLineSub", BM_CompSingleLineSub);
  c->Iterations(DEFAULT_SEED_COUNT);

  auto* d = benchmark::RegisterBenchmark(
      "CompositionOptimizer/MultiLineSingleEdit", BM_CompMultiLineSingleEdit);
  for (int v : {2, 3, 5}) d->Arg(v);
  d->Iterations(DEFAULT_SEED_COUNT);

  auto* e = benchmark::RegisterBenchmark(
      "CompositionOptimizer/MultiLineMultiEdit", BM_CompMultiLineMultiEdit);
  for (int v : {5, 10, 20, 40}) e->Arg(v);
  e->Iterations(DEFAULT_SEED_COUNT);

  return 0;
}();
