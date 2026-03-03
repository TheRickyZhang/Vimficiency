// tests/Benchmarks/EditOptimizerBench.cpp
//
// Performance benchmarks for EditOptimizer using Google Benchmark.
//
// Run: ./build/tests/vimficiency_benchmarks --benchmark_filter="EditOptimizer.*"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/EditBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
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
  EditBoundary boundary;

  // Pure deletion: delete all content
  BenchmarkSetup(const Lines& lines)
      : initialLines(lines),
        goalLines({""}),
        boundary(lines, CursorPos(0, 0), lines.endPos()) {}

  // Custom goal
  BenchmarkSetup(const Lines& initial, const Lines& goal, const EditBoundary& b)
      : initialLines(initial), goalLines(goal), boundary(b) {}
};

static void runBenchmark(const BenchmarkSetup& cfg,
                         const EditOptimizerParams& params,
                         EditSearchStats& outStats) {
  EditOptimizer opt(benchConfig);
  if (isPureDeletionGoal(cfg.goalLines)) {
    EditResult result =
        opt.optimizePureDeletion(cfg.initialLines, cfg.boundary, params);
    accumulateStats(outStats, result.getStats());
    return;
  }

  EditResult result = opt.optimizeEdit(cfg.initialLines, cfg.goalLines, cfg.boundary, params);
  accumulateStats(outStats, result.getStats());
}

static EditOptimizerParams withCap(EditOptimizerParams p, const BenchmarkSetup& s) {
  return p.withMaxResults(max(10, s.initialLines.totalPositions() / 4));
}

static vector<size_t> collectPerStartCounts(const EditResult& result, const Lines& lines) {
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

  EditOptimizer opt(benchConfig);
  for (const Lines& lines : candidates) {
    BenchmarkSetup setup(lines);
    EditOptimizerParams params = EditOptimizerParams{}
        .withMaxResults(5000)
        .withMaxTotalPops(200000)
        .withMaxMultiplePerStartPosition(perStartCap);
    EditResult result = opt.optimizePureDeletion(setup.initialLines, setup.boundary, params);
    if (result.getStats().stopReason != SearchStopReason::AllResultsFound) continue;

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

static void BM_EditBufferSize(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  EditSearchStats totalStats;
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
  EditSearchStats totalStats;
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
  EditSearchStats totalStats;
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
  int maxPops = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  EditSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(15, 30));
    EditOptimizerParams params;
    params.maxTotalPops = maxPops;
    runBenchmark(setup, withCap(params, setup), totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_EditMultiLineDelete(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  EditSearchStats totalStats;
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
  EditSearchStats totalStats;
  for (auto _ : state) {
    RandomGen::seed(42);
    Lines fullBuffer = generateBuffer(10, 30);

    BenchmarkSetup setup(fullBuffer);
    switch (boundaryType) {
      case 0: // None
        break;
      case 1: { // Prefix
        CursorPos firstPos(0, 15);
        CursorPos endPos = fullBuffer.endPos();
        Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
        EditBoundary boundary(fullBuffer, firstPos, endPos);
        setup = BenchmarkSetup(editRegion, {""}, boundary);
        break;
      }
      case 2: { // Suffix
        CursorPos firstPos(0, 0);
        int lastLine = fullBuffer.lastLine();
        int midCol = static_cast<int>(fullBuffer[lastLine].size()) / 2;
        CursorPos endPos(lastLine, midCol + 1);
        Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
        EditBoundary boundary(fullBuffer, firstPos, endPos);
        setup = BenchmarkSetup(editRegion, {""}, boundary);
        break;
      }
      case 3: { // Both
        CursorPos firstPos(0, 10);
        int lastLine = fullBuffer.lastLine();
        int lastLineLen = static_cast<int>(fullBuffer[lastLine].size());
        CursorPos endPos(lastLine, max(1, lastLineLen - 10 + 1));
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
  EditSearchStats totalStats;
  for (auto _ : state) {
    BenchmarkSetup setup({""});

    auto makeSetup = [](const Lines& buffer, const Lines& goal,
                        CursorPos editBegin, CursorPos editEnd) {
      Lines editRegion = buffer.getSpan(editBegin, editEnd);
      EditBoundary boundary(buffer, editBegin, editEnd);
      return BenchmarkSetup(editRegion, goal, boundary);
    };

    switch (caseNum) {
      case 0: { // 2L->1w
        Lines buffer = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
        Lines goal = {"Florida"};
        setup = makeSetup(buffer, goal, CursorPos(0, 23), CursorPos(1, 19));
        break;
      }
      case 1: { // 3L->1w
        Lines buffer = {"The quick brown fox jumps over the lazy dog",
                        "and then runs around the park",
                        "before going home to sleep"};
        Lines goal = {"walked away"};
        setup = makeSetup(buffer, goal, CursorPos(0, 20), CursorPos(2, 26));
        break;
      }
      case 2: { // 5L+bnd
        Lines buffer = {"prefix stuff delete me line one",
                        "delete me line two",
                        "delete me line three",
                        "delete me line four",
                        "delete me line five and suffix here"};
        Lines goal = {"replaced"};
        setup = makeSetup(buffer, goal, CursorPos(0, 13), CursorPos(4, 22));
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
  EditSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines buffer = generateBuffer(numLines, 25);
    Lines goal = {"replacement"};
    EditBoundary boundary(buffer, CursorPos(0, 0), buffer.endPos());
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
  EditSearchStats totalStats;
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
  EditSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    // Generate full buffer, carve out edit region with prefix/suffix
    Lines fullBuffer = randomLines(numLines + 1, 8, 15);
    int prefixLen = min(4, static_cast<int>(fullBuffer[0].size()) / 2);
    int lastLine = static_cast<int>(fullBuffer.size()) - 1;
    int suffixLen = min(4, static_cast<int>(fullBuffer[lastLine].size()) / 2);
    CursorPos firstPos(0, prefixLen);
    CursorPos endPos(lastLine, static_cast<int>(fullBuffer[lastLine].size()) - suffixLen);
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

  EditSearchStats totalStats;
  int allResultsFoundCount = 0;
  for (auto _ : state) {
    EditOptimizer opt(benchConfig);
    EditOptimizerParams params = EditOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxTotalPops(maxPops)
        .withMaxMultiplePerStartPosition(perStartCap);
    EditResult result = opt.optimizePureDeletion(
        mixedSetup->initialLines, mixedSetup->boundary, params);
    accumulateStats(totalStats, result.getStats());
    if (result.getStats().stopReason == SearchStopReason::AllResultsFound) {
      allResultsFoundCount++;
    }
  }

  setSearchCounters(state, totalStats);
  state.counters["AllStop"] = benchmark::Counter(
      allResultsFoundCount, benchmark::Counter::kAvgIterations);
  state.counters["MaxR"] = static_cast<double>(maxResults);
  state.counters["PerStartCap"] = static_cast<double>(perStartCap);
}

// Sensitivity to maxMultiplePerStartPosition (per-start result cap).
// maxResults is kept high so per-start cap drives result collection.
static void BM_EditPerStartCap(benchmark::State& state) {
  int perStartCap = static_cast<int>(state.range(0));
  constexpr int maxResults = 10000;
  constexpr int maxPops = 20000;
  auto& seedMgr = SeedManager::instance();

  EditSearchStats totalStats;
  int allResultsFoundCount = 0;
  int maxResultsFoundCount = 0;
  int maxPopsStopCount = 0;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));

    BufferShape shape = BufferShape::CodeLike;
    if (iter % 3 == 1) {
      shape = BufferShape::Uniform;
    } else if (iter % 3 == 2) {
      shape = BufferShape::Prose;
    }
    int numLines = RandomGen::range(2, 6);
    int avgLen = RandomGen::range(6, 16);
    BenchmarkSetup setup(generateBuffer(numLines, avgLen, shape));

    EditOptimizer opt(benchConfig);
    EditOptimizerParams params = EditOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxTotalPops(maxPops)
        .withMaxMultiplePerStartPosition(perStartCap);
    EditResult result = opt.optimizePureDeletion(
        setup.initialLines, setup.boundary, params);
    accumulateStats(totalStats, result.getStats());
    if (result.getStats().stopReason == SearchStopReason::AllResultsFound) {
      allResultsFoundCount++;
    } else if (result.getStats().stopReason == SearchStopReason::MaxResultsFound) {
      maxResultsFoundCount++;
    } else if (result.getStats().stopReason == SearchStopReason::MaxPopsReached) {
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

  // High maxResults sensitivity with per-start multiplicity enabled.
  registerArgBenchmark("EditOpt/MaxResultsTerminal", BM_EditMaxResultsTerminal,
                       {1000, 5000, 20000, 100000});

  // Per-start cap sensitivity with high maxResults.
  registerArgBenchmark("EditOpt/PerStartCap", BM_EditPerStartCap,
                       {1, 2, 4, 8, 12});

  return 0;
}();

} // namespace
