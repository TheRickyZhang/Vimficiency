// tests/Benchmarks/TreeDiffBench.cpp
//
// Isolated performance benchmarks for TreeDiff::calculate (the composition
// diff-generation phase). The composition benchmarks measure the whole
// optimize() pipeline; these isolate diff generation alone so we can fit an
// empirical big-O against buffer size, line length, edit count, and the
// diff-open penalty, and contrast flat vs paragraphed buffers (fanout).
//
// Each size/length/edit-count sweep reports SetComplexityN so Google Benchmark
// auto-fits the dominant term. The "Chars" counter is the flat character count
// (newlines included) used as the n proxy; "Diffs" is the number of diff
// regions produced.
//
// Run: ./build/tests/vimfy_benchmarks --benchmark_filter="TreeDiff/.*"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/TreeDiff.h"
#include "Types/Lines.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

static Config treeDiffConfig = Config::uniform();

struct DiffSetup {
  Lines initial;
  Lines goal;
};

static int totalChars(const Lines& lines) {
  return ssize(lines.flatten());
}

// blankEvery == 0 -> one flat paragraph; otherwise a blank line is inserted
// after every `blankEvery` content lines, producing distinct paragraphs.
static Lines buildBuffer(int numLines, int avgLen, int blankEvery) {
  Lines lines;
  for (int i = 0; i < numLines; i++) {
    if (blankEvery > 0 && i > 0 && i % blankEvery == 0) lines.push_back("");
    lines.push_back(randomProseLine(avgLen));
  }
  return lines;
}

// Rewrite `editCount` lines spread across the buffer, preserving line length so
// only content (not structure) differs.
static DiffSetup makeDiffSetup(int numLines, int avgLen, int editCount,
                               int blankEvery, int seed) {
  RandomGen::seed(seed);
  Lines initial = buildBuffer(numLines, avgLen, blankEvery);
  Lines goal = initial;
  const int n = ssize(goal);
  const int edits = min(max(1, editCount), n);
  for (int e = 0; e < edits; e++) {
    int line = (e * (n - 1)) / max(1, edits - 1);
    line = min(line, n - 1);
    if (goal[line].empty()) continue;  // skip paragraph separators
    int len = max(1, static_cast<int>(initial[line].size()));
    goal[line] = randomProseLine(len);
    if (goal[line] == initial[line]) goal[line] += "x";
  }
  if (goal == initial && !goal.empty()) goal[0] += "x";
  return {std::move(initial), std::move(goal)};
}

static vector<DiffSetup> seededSetups(int numLines, int avgLen, int editCount,
                                      int blankEvery) {
  auto& sm = SeedManager::instance();
  vector<DiffSetup> setups;
  setups.reserve(DEFAULT_SEED_COUNT);
  for (int i = 0; i < DEFAULT_SEED_COUNT; i++)
    setups.push_back(
        makeDiffSetup(numLines, avgLen, editCount, blankEvery, sm.getSeed(i)));
  return setups;
}

static void runDiffBench(benchmark::State& state,
                         const vector<DiffSetup>& setups,
                         TreeDiff::CostOptions options) {
  long long diffCount = 0;
  int iter = 0;
  for (auto _ : state) {
    const DiffSetup& s = setups[iter % ssize(setups)];
    auto diffs =
        TreeDiff::calculate(s.initial, s.goal, treeDiffConfig, options);
    benchmark::DoNotOptimize(diffs);
    diffCount += ssize(diffs);
    iter++;
  }
  state.counters["Chars"] = totalChars(setups[0].initial);
  state.counters["Diffs"] =
      benchmark::Counter(diffCount, benchmark::Counter::kAvgIterations);
}

// =============================================================================
// Sweeps
// =============================================================================

// Size, proportional edits (~1 edit per 8 lines). The realistic regime: diff
// count grows with the buffer.
static void BM_TreeDiffSizeProp(benchmark::State& state) {
  const int numLines = state.range(0);
  auto setups = seededSetups(numLines, 30, max(1, numLines / 8), /*blankEvery=*/4);
  runDiffBench(state, setups, {});
  state.SetComplexityN(totalChars(setups[0].initial));
}

// Size, constant 3 edits regardless of buffer. Isolates the cost of aligning
// the mostly-matching tree from the cost of the diffs themselves.
static void BM_TreeDiffSizeSparse(benchmark::State& state) {
  const int numLines = state.range(0);
  auto setups = seededSetups(numLines, 30, /*editCount=*/3, /*blankEvery=*/4);
  runDiffBench(state, setups, {});
  state.SetComplexityN(totalChars(setups[0].initial));
}

// Line length at fixed line count. Exercises the within-line word/char levels.
static void BM_TreeDiffLineLength(benchmark::State& state) {
  const int avgLen = state.range(0);
  auto setups = seededSetups(16, avgLen, /*editCount=*/3, /*blankEvery=*/4);
  runDiffBench(state, setups, {});
  state.SetComplexityN(totalChars(setups[0].initial));
}

// Edit count at fixed buffer. Fits cost growth in number of changed regions.
static void BM_TreeDiffEditCount(benchmark::State& state) {
  const int editCount = state.range(0);
  auto setups = seededSetups(40, 24, editCount, /*blankEvery=*/4);
  runDiffBench(state, setups, {});
  state.SetComplexityN(editCount);
}

// diff-open penalty. Higher penalty merges more aggressively, changing how
// deep the recursion goes; discrete sweep, no complexity fit.
static void BM_TreeDiffPenalty(benchmark::State& state) {
  const double penalty = static_cast<double>(state.range(0));
  auto setups = seededSetups(24, 24, /*editCount=*/6, /*blankEvery=*/4);
  runDiffBench(state, setups, TreeDiff::CostOptions{.diffOpenPenalty = penalty});
}

// Flat (one paragraph) vs paragraphed at equal size. Probes the line-level
// fanout that drives the inner-DP term.
static void BM_TreeDiffFanout(benchmark::State& state) {
  const int blankEvery = state.range(0);  // 0 = flat
  auto setups = seededSetups(48, 24, /*editCount=*/6, blankEvery);
  runDiffBench(state, setups, {});
  state.SetComplexityN(totalChars(setups[0].initial));
}

// =============================================================================
// Registration
// =============================================================================

// Chain every Arg of a family under one registration so Complexity() can fit
// across the whole sweep instead of degenerating to one point per Arg.
static benchmark::internal::Benchmark* sweep(
    const char* name, benchmark::internal::Function* fn,
    const vector<int>& args) {
  auto* b = benchmark::RegisterBenchmark(name, fn)->Unit(benchmark::kMillisecond);
  for (int arg : args) b->Arg(arg);
  return b;
}

static int registerTreeDiffBenchmarks = []() {
  // Size ranges are kept modest because the current implementation is
  // superlinear; extend the ceilings once the algorithm is fixed.
  const vector<int> sizeArgs{4, 8, 12, 16, 24, 32};
  sweep("TreeDiff/SizeProp", BM_TreeDiffSizeProp, sizeArgs)->Complexity();
  sweep("TreeDiff/SizeSparse", BM_TreeDiffSizeSparse, sizeArgs)->Complexity();

  sweep("TreeDiff/LineLength", BM_TreeDiffLineLength, {5, 10, 20, 40, 80, 160})
      ->Complexity();

  sweep("TreeDiff/EditCount", BM_TreeDiffEditCount, {1, 2, 4, 8, 16})
      ->Complexity();

  sweep("TreeDiff/Penalty", BM_TreeDiffPenalty, {2, 4, 8, 16, 32});

  // 0 = one flat paragraph (line-level fanout worst case), 3 = paragraphed.
  sweep("TreeDiff/Fanout", BM_TreeDiffFanout, {0, 3});

  return 0;
}();

}  // namespace
