// tests/Benchmarks/TransformOptimizerBench.cpp
//
// Performance benchmarks for TransformOptimizer using Google Benchmark.
//
// Run: ./build/tests/vimfy_benchmarks --benchmark_filter="TransformOptimizer.*"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/TransformBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Utils/OptimizerCaseCatalog.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

// =============================================================================
// Benchmark Infrastructure
// =============================================================================

static Config benchConfig = Config::uniform();

static bool isPureDeletionGoal(const Lines& goalLines) {
  return goalLines.empty() || (goalLines.size() == 1 && goalLines[0].empty());
}

struct BenchmarkSetup {
  Lines initialLines;
  Lines goalLines;
  TransformBoundary boundary;

  // Pure deletion: delete all content
  BenchmarkSetup(const Lines& lines)
      : initialLines(lines),
        goalLines({""}),
        boundary(lines, CursorPos(0, 0), lines.endPos()) {}

  // Custom goal
  BenchmarkSetup(const Lines& initial, const Lines& goal, const TransformBoundary& b)
      : initialLines(initial), goalLines(goal), boundary(b) {}
};

static void runBenchmark(const BenchmarkSetup& cfg,
                         const TransformOptimizerParams& params,
                         TransformSearchStats& outStats) {
  TransformOptimizer opt(benchConfig);
  if (isPureDeletionGoal(cfg.goalLines)) {
    TransformResult result =
        opt.optimizePureDeletion(cfg.initialLines, cfg.boundary, params);
    accumulateStats(outStats, result.getStats());
    return;
  }

  TransformResult result = opt.optimizeTransform(cfg.initialLines, cfg.goalLines, cfg.boundary, params);
  accumulateStats(outStats, result.getStats());
}

static TransformOptimizerParams withCap(TransformOptimizerParams p, const BenchmarkSetup& s) {
  return p.withMaxResults(max(10, s.initialLines.totalPositions() / 4));
}

template<typename MakeSetup>
static vector<BenchmarkSetup> makeSeededSetups(MakeSetup makeSetup) {
  auto& seedMgr = SeedManager::instance();
  vector<BenchmarkSetup> setups;
  setups.reserve(DEFAULT_SEED_COUNT);
  for (int i = 0; i < DEFAULT_SEED_COUNT; ++i) {
    RandomGen::seed(seedMgr.getSeed(i));
    setups.push_back(makeSetup(i));
  }
  return setups;
}

static BenchmarkSetup makeBoundarySetup(int boundaryType) {
  RandomGen::seed(42);
  Lines fullBuffer = generateBuffer(10, 30);

  BenchmarkSetup setup(fullBuffer);
  switch (boundaryType) {
    case 0:
      break;
    case 1: {
      CursorPos firstPos(0, 15);
      CursorPos endPos = fullBuffer.endPos();
      Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
      TransformBoundary boundary(fullBuffer, firstPos, endPos);
      setup = BenchmarkSetup(editRegion, {""}, boundary);
      break;
    }
    case 2: {
      CursorPos firstPos(0, 0);
      int lastLine = fullBuffer.lastLine();
      int midCol = static_cast<int>(fullBuffer[lastLine].size()) / 2;
      CursorPos endPos(lastLine, midCol + 1);
      Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
      TransformBoundary boundary(fullBuffer, firstPos, endPos);
      setup = BenchmarkSetup(editRegion, {""}, boundary);
      break;
    }
    case 3: {
      CursorPos firstPos(0, 10);
      int lastLine = fullBuffer.lastLine();
      int lastLineLen = static_cast<int>(fullBuffer[lastLine].size());
      CursorPos endPos(lastLine, max(1, lastLineLen - 10 + 1));
      Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
      TransformBoundary boundary(fullBuffer, firstPos, endPos);
      setup = BenchmarkSetup(editRegion, {""}, boundary);
      break;
    }
  }
  return setup;
}

static vector<size_t> collectPerStartCounts(const TransformResult& result, const Lines& lines) {
  vector<size_t> counts;
  for (int line = 0; line < static_cast<int>(lines.size()); line++) {
    for (int col = 0; col < lines[line].effectiveSize(); col++) {
      counts.push_back(result.resultsAt(line, col).size());
    }
  }
  return counts;
}

static optional<BenchmarkSetup> findMixedTerminalSetup(int perStartCap) {
  static const array<Lines, 12> candidates = {
      Lines{"ab"},
      Lines{"abc"},
      Lines{"abcd"},
      Lines{"a", "b"},
      Lines{"ab", "c"},
      Lines{"abc", "d"},
      Lines{"a", "bc"},
      Lines{"ab", "cd"},
      Lines{"abc", "de"},
      Lines{"a", "", "b"},
      Lines{"ab", "", "cd"},
      Lines{"abc", "", "def"}
  };

  TransformOptimizer opt(benchConfig);
  for (const Lines& lines : candidates) {
    BenchmarkSetup setup(lines);
    TransformOptimizerParams params = TransformOptimizerParams{}
        .withMaxResults(5000)
        .withMaxNodesPopped(200000)
        .withMaxResultsPerStartPos(perStartCap);
    TransformResult result = opt.optimizePureDeletion(setup.initialLines, setup.boundary, params);
    if (result.getStats().stopReason() != SearchStopReason::AllResultsFound) continue;

    bool hasCapped = false;
    bool hasUncapped = false;
    for (size_t c : collectPerStartCounts(result, lines)) {
      hasCapped |= (c >= static_cast<size_t>(perStartCap));
      hasUncapped |= (c > 0 && c < static_cast<size_t>(perStartCap));
    }
    if (hasCapped && hasUncapped) {
      return setup;
    }
  }
  return nullopt;
}

// =============================================================================
// Benchmark Functions
// =============================================================================

// Drives one catalog case (BufferSize/LineLength/MultiLineDelete pure deletions
// and MultiLineEdit transforms). The catalog's buildEditSetup fixes the buffer +
// maxResults cap per seed, so the explorer traces the identical case.
static void BM_EditCatalogCase(benchmark::State& state, EditCaseSpec spec) {
  vector<EditSetup> setups;
  setups.reserve(DEFAULT_SEED_COUNT);
  for (int i = 0; i < DEFAULT_SEED_COUNT; ++i)
    setups.push_back(buildEditSetup(spec, i));
  TransformSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    const EditSetup& s = setups[iter % static_cast<int>(setups.size())];
    TransformOptimizer opt(benchConfig);
    if (s.goalKind == EditGoalKind::PureDeletion) {
      accumulateStats(totalStats,
                      opt.optimizePureDeletion(s.initialLines, s.boundary, s.params).getStats());
    } else {
      accumulateStats(totalStats,
                      opt.optimizeTransform(s.initialLines, s.goalLines, s.boundary, s.params).getStats());
    }
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditBufferShape(benchmark::State& state, BufferShape shape) {
  auto setups = makeSeededSetups([&](int) {
    return BenchmarkSetup(generateBuffer(10, 30, shape));
  });
  TransformSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    const auto& setup = setups[iter % static_cast<int>(setups.size())];
    runBenchmark(setup, withCap({}, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditSearchDepth(benchmark::State& state) {
  int maxPops = static_cast<int>(state.range(0));
  auto setups = makeSeededSetups([](int) {
    return BenchmarkSetup(generateBuffer(15, 30));
  });
  TransformSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    const auto& setup = setups[iter % static_cast<int>(setups.size())];
    TransformOptimizerParams params;
    params.maxNodesPopped = maxPops;
    runBenchmark(setup, withCap(params, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_TransformBoundaryConstraints(benchmark::State& state, int boundaryType) {
  auto setup = makeBoundarySetup(boundaryType);
  TransformSearchStats totalStats;
  for (auto _ : state) {
    int mr = max(10, setup.initialLines.totalPositions() / 4);
    runBenchmark(setup, TransformOptimizerParams{}.withMaxResults(mr), totalStats);
  }
  setSearchCounters(state, totalStats);
}

// Small full-content change: matches MultiLine_FullBufferChange pattern
// (2-3 lines × 4-8 chars, initial → different goal, default params)
static void BM_EditSmallChange(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto setups = makeSeededSetups([&](int) {
    Lines source = randomLines(numLines, 4, 8);
    Lines goal = randomLines(numLines, 4, 8);
    if (source == goal) goal[0] = "changed";
    TransformBoundary boundary(source, {0, 0}, source.endPos());
    return BenchmarkSetup(source, goal, boundary);
  });
  TransformSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    const auto& setup = setups[iter % static_cast<int>(setups.size())];
    runBenchmark(setup, {}, totalStats);  // default maxResults, no cap
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// Small embedded change: matches MultiLine_EmbeddedChange pattern
// (edit region with prefix/suffix boundary, small lines)
static void BM_EditSmallEmbeddedChange(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto setups = makeSeededSetups([&](int) {
    Lines fullBuffer = randomLines(numLines + 1, 8, 15);
    int prefixLen = min(4, static_cast<int>(fullBuffer[0].size()) / 2);
    int lastLine = static_cast<int>(fullBuffer.size()) - 1;
    int suffixLen = min(4, static_cast<int>(fullBuffer[lastLine].size()) / 2);
    CursorPos firstPos(0, prefixLen);
    CursorPos endPos(lastLine, static_cast<int>(fullBuffer[lastLine].size()) - suffixLen);
    if (endPos.col <= 0) endPos.col = static_cast<int>(fullBuffer[lastLine].size());
    Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
    TransformBoundary boundary(fullBuffer, firstPos, endPos);
    Lines goal = randomLines(static_cast<int>(editRegion.size()), 4, 8);
    return BenchmarkSetup(editRegion, goal, boundary);
  });
  TransformSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    const auto& setup = setups[iter % static_cast<int>(setups.size())];
    runBenchmark(setup, {}, totalStats);  // default maxResults, no cap
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// Sensitivity to high maxResults in mixed capped/exhausted per-start scenario.
// Expected: runtime and explored nodes remain stable once terminal-state stop dominates.
static void BM_EditMaxResultsTerminal(benchmark::State& state) {
  int maxResults = static_cast<int>(state.range(0));
  constexpr int perStartCap = 4;
  constexpr int maxPops = 200000;

  static optional<BenchmarkSetup> mixedSetup = findMixedTerminalSetup(perStartCap);
  if (!mixedSetup.has_value()) {
    state.SkipWithError("No mixed terminal setup found for EditOpt/MaxResultsTerminal");
    return;
  }

  TransformSearchStats totalStats;
  int allResultsFoundCount = 0;
  for (auto _ : state) {
    TransformOptimizer opt(benchConfig);
    TransformOptimizerParams params = TransformOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxPops)
        .withMaxResultsPerStartPos(perStartCap);
    TransformResult result = opt.optimizePureDeletion(
        mixedSetup->initialLines, mixedSetup->boundary, params);
    accumulateStats(totalStats, result.getStats());
    if (result.getStats().stopReason() == SearchStopReason::AllResultsFound) {
      allResultsFoundCount++;
    }
  }

  setSearchCounters(state, totalStats);
  state.counters["AllStop"] = benchmark::Counter(
      allResultsFoundCount, benchmark::Counter::kAvgIterations);
  state.counters["MaxR"] = static_cast<double>(maxResults);
  state.counters["PerStartCap"] = static_cast<double>(perStartCap);
}

// Sensitivity to maxResultsPerStartPos (per-start result cap).
// maxResults is kept high so per-start cap drives result collection.
static void BM_EditPerStartCap(benchmark::State& state) {
  int perStartCap = static_cast<int>(state.range(0));
  constexpr int maxResults = 10000;
  constexpr int maxPops = 20000;
  auto setups = makeSeededSetups([](int iter) {
    BufferShape shape = BufferShape::CodeLike;
    if (iter % 3 == 1) {
      shape = BufferShape::Uniform;
    } else if (iter % 3 == 2) {
      shape = BufferShape::Prose;
    }
    int numLines = RandomGen::range(2, 6);
    int avgLen = RandomGen::range(6, 16);
    return BenchmarkSetup(generateBuffer(numLines, avgLen, shape));
  });

  TransformSearchStats totalStats;
  int allResultsFoundCount = 0;
  int maxResultsFoundCount = 0;
  int maxPopsStopCount = 0;
  int iter = 0;
  for (auto _ : state) {
    const auto& setup = setups[iter % static_cast<int>(setups.size())];

    TransformOptimizer opt(benchConfig);
    TransformOptimizerParams params = TransformOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxPops)
        .withMaxResultsPerStartPos(perStartCap);
    TransformResult result = opt.optimizePureDeletion(
        setup.initialLines, setup.boundary, params);
    accumulateStats(totalStats, result.getStats());
    if (result.getStats().stopReason() == SearchStopReason::AllResultsFound) {
      allResultsFoundCount++;
    } else if (result.getStats().stopReason() == SearchStopReason::MaxResultsFound) {
      maxResultsFoundCount++;
    } else if (result.getStats().stopReason() == SearchStopReason::MaxPopsReached) {
      maxPopsStopCount++;
    }
    iter++;
  }

  setSearchCounters(state, totalStats);
  state.counters["AllStop"] = benchmark::Counter(
      allResultsFoundCount, benchmark::Counter::kAvgIterations);
  state.counters["MaxStop"] = benchmark::Counter(
      maxResultsFoundCount, benchmark::Counter::kAvgIterations);
  state.counters["PopStop"] = benchmark::Counter(
      maxPopsStopCount, benchmark::Counter::kAvgIterations);
  state.counters["PerStartCap"] = static_cast<double>(perStartCap);
  state.counters["MaxR"] = static_cast<double>(maxResults);
}

// =============================================================================
// Registration
// =============================================================================

// Helper to register parameterized (Arg) benchmarks
static void registerArgBenchmark(const string& name, void(*fn)(benchmark::State&),
                                 const vector<int>& args) {
  auto* b = benchmark::RegisterBenchmark(name, fn);
  for (int v : args) b->Arg(v);
}

// Static initialization block to register all benchmarks
static int registerEditBenchmarks = []() {
  // Explorable cases (BufferSize/LineLength/MultiLineDelete/MultiLineEdit) come
  // from the shared catalog so the benchmark and explore tool agree on names +
  // sizes. Each registers under its full "EditOpt/<category>/<param>" name.
  for (const auto& spec : editCaseCatalog()) {
    benchmark::RegisterBenchmark("EditOpt/" + spec.name, BM_EditCatalogCase, spec);
  }

  // --- Bench-only tuning sweeps + micro-benchmarks (not surfaced in explore) ---

  // BufferShape
  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    benchmark::RegisterBenchmark(
        "EditOpt/BufferShape/" + name, BM_EditBufferShape, shape);
  }

  // SearchDepth
  registerArgBenchmark("EditOpt/SearchDepth", BM_EditSearchDepth,
                       {1000, 5000, 10000, 50000});

  // BoundaryConstraints
  for (const auto& [name, type] : vector<pair<string, int>>{
           {"None", 0}, {"Prefix", 1}, {"Suffix", 2}, {"Both", 3}}) {
    benchmark::RegisterBenchmark("EditOpt/Boundary/" + name, BM_TransformBoundaryConstraints, type);
  }

  // SmallChange - matches correctness test pattern (small region, full content change)
  registerArgBenchmark("EditOpt/SmallChange", BM_EditSmallChange,
                       {1, 2, 3});

  // SmallEmbeddedChange - small edit region with prefix/suffix boundary
  registerArgBenchmark("EditOpt/SmallEmbeddedChange", BM_EditSmallEmbeddedChange,
                       {1, 2, 3});

  // High maxResults sensitivity with per-start multiplicity enabled.
  registerArgBenchmark("EditOpt/MaxResultsTerminal", BM_EditMaxResultsTerminal,
                       {1000, 5000, 20000, 100000});

  // Per-start cap sensitivity with high maxResults.
  registerArgBenchmark("EditOpt/PerStartCap", BM_EditPerStartCap,
                       {1, 2, 4, 8, 12});

  return 0;
}();

} // namespace
