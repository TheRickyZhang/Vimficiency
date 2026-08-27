// tests/Benchmarks/VimDiffBench.cpp
//
// Planner-only timing for VimDiff::calculate — isolates diff planning from the
// composition search that CompositionOpt/* times end-to-end. Guards the O(N^3)
// G/D/F DP and the O(N^2) oracle tables. Not in the dashboard suites; run ad hoc:
//   ./build/tests/vimfy_benchmarks --benchmark_filter="VimDiffPlan.*"

#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

namespace {

static Config benchConfig = Config::uniform();

void BM_VimDiffPlan(benchmark::State& state, CompositionCaseSpec spec) {
  vector<CompositionSetup> setups;
  setups.reserve(DEFAULT_SEED_COUNT);
  for (int i = 0; i < DEFAULT_SEED_COUNT; ++i)
    setups.push_back(buildCompositionSetup(spec, i));
  int iter = 0;
  for (auto _ : state) {
    const CompositionSetup& s = setups[iter % (int)setups.size()];
    auto plans = VimDiff::calculate(s.initial, s.goal, benchConfig,
                                    VimDiff::CostOptions{
                                        .diffOpenPenalty = s.params.diffOpenPenalty,
                                        .moveDeleteScale = s.params.moveDeleteScale,
                                    });
    benchmark::DoNotOptimize(plans);
    iter++;
  }
}

static int registerVimDiffBenchmarks = []() {
  // Same shapes as CompositionOpt/BufferSize*, planner-only. ~100 lines (~2k
  // flat chars) is where the cubic DP dominates; larger slices approach the
  // MAX_PLANNER_CELLS guard — see diff-generation.md § Complexity.
  for (int numLines : {10, 20, 40, 100}) {
    CompositionCaseSpec spec{"BufferSize/" + to_string(numLines), numLines, 20, 5};
    benchmark::RegisterBenchmark("VimDiffPlan/" + spec.name, BM_VimDiffPlan, spec)
        ->Unit(benchmark::kMillisecond);
  }
  for (int editCount : {2, 5, 10}) {
    CompositionCaseSpec spec{"EditCount/" + to_string(editCount), 15, 20, editCount};
    benchmark::RegisterBenchmark("VimDiffPlan/" + spec.name, BM_VimDiffPlan, spec)
        ->Unit(benchmark::kMillisecond);
  }
  return 0;
}();

}  // namespace
