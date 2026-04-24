// tests/Benchmarks/NavOptimizerBench.cpp
//
// Performance benchmarks for NavOptimizer using Google Benchmark.
//
// Run: ./build/tests/vimficiency_benchmarks --benchmark_filter="NavOptimizer.*"

#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <benchmark/benchmark.h>

#include "Benchmarks/BenchUtils.h"
#include "Boundary/NavBoundary.h"
#include "Effort/EffortBank.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavExplorer.h"
#include "Optimizer/NavOptimizer/NavHeuristic.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Optimizer/SearchFrontier.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

// =============================================================================
// A/B Comparison Configuration
// =============================================================================

constexpr bool ENABLE_COMPARISON = false;

static NavOptimizerParams navParamsA() {
  return NavOptimizerParams{};
}

static NavOptimizerParams navParamsB() {
  return NavOptimizerParams{}.withDirectionalPruning(false);
}

static NavOptimizerRangeParams rangeParamsA() {
  return NavOptimizerRangeParams{};
}

static NavOptimizerRangeParams rangeParamsB() {
  return NavOptimizerRangeParams{}.withDirectionalPruning(false);
}

// =============================================================================
// Benchmark Infrastructure
// =============================================================================

static Config benchConfig = Config::uniform();
using NavPriorityQueue =
    priority_queue<NavState, vector<NavState>, greater<NavState>>;

struct BenchmarkSetup {
  Lines lines;
  NavBoundary boundary{};
  CursorPos firstPos;
  CursorPos lastPos;

  BenchmarkSetup(const Lines& lines) : lines(lines) {
    firstPos = randomFirstPos(this->lines);
    lastPos = randomLastPos(this->lines);
    CursorPos boundaryEnd(lastPos.line, lastPos.col + 1);
    boundary = NavBoundary(this->lines, firstPos, boundaryEnd, true, true);
  }
};

struct RangeBenchmarkSetup {
  Lines lines;
  NavBoundary boundary{};
  CursorPos initialPos;
  CursorPos rangeBegin;
  CursorPos rangeEnd;

  RangeBenchmarkSetup(const Lines& lines) : lines(lines) {
    initialPos = {0, 0};
    int lastLine = lines.lastLine();
    int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
    int rangeSize = min(DEFAULT_RANGE_SIZE, lastLineLen);
    int startCol = max(0, lastLineLen - rangeSize);
    rangeBegin = {lastLine, startCol};
    rangeEnd = {lastLine, lastLineLen};
    boundary = NavBoundary(lines, rangeBegin, rangeEnd, true, true);
  }

  RangeBenchmarkSetup(const Lines& lines, int rangeChars, int rangeLines = 1) : lines(lines) {
    initialPos = {0, 0};
    int lastLine = lines.lastLine();
    int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
    rangeLines = min(rangeLines, lastLine + 1);

    if (rangeLines == 1) {
      int actualChars = min(rangeChars, lastLineLen);
      int startCol = max(0, lastLineLen - actualChars);
      rangeBegin = {lastLine, startCol};
      rangeEnd = {lastLine, lastLineLen};
    } else {
      int beginLine = lastLine - rangeLines + 1;
      int beginLineLen = max(1, static_cast<int>(lines[beginLine].size()));
      int startCol = max(0, beginLineLen / 2);
      rangeBegin = {beginLine, startCol};
      rangeEnd = {lastLine, lastLineLen};
    }
    boundary = NavBoundary(lines, rangeBegin, rangeEnd, true, true);
  }
};

struct DispatchExploreCase {
  static CursorPos computeRangeBegin(const Lines& lines, int rangeChars) {
    int lastLine = lines.lastLine();
    int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
    int actualChars = min(rangeChars, lastLineLen);
    int startCol = max(0, lastLineLen - actualChars);
    return {lastLine, startCol};
  }

  static CursorPos computeRangeEnd(const Lines& lines) {
    int lastLine = lines.lastLine();
    int lastLineLen = max(1, static_cast<int>(lines[lastLine].size()));
    return {lastLine, lastLineLen};
  }

  Lines lines;
  NavContext navContext;
  NavOptimizerRangeParams params;
  CursorPos initialPos;
  CursorPos rangeBegin;
  CursorPos rangeEnd;
  CharInterval goalRange;
  NavBoundary boundary;
  BufferIndex bufferIndex;
  NavExplorer explorer;
  NavState state;
  EffortBank bank;

  DispatchExploreCase(const Lines& sourceLines,
                      NavOptimizerRangeParams paramsIn = {},
                      CursorPos initialPosIn = CursorPos(0, 0),
                      int rangeChars = DEFAULT_RANGE_SIZE)
      : lines(sourceLines),
        navContext(),
        params(paramsIn),
        initialPos(initialPosIn),
        rangeBegin(computeRangeBegin(lines, rangeChars)),
        rangeEnd(computeRangeEnd(lines)),
        goalRange(toMotionInterval(lines, CharRange(rangeBegin, rangeEnd))),
        boundary(lines, rangeBegin, rangeEnd, true, true),
        bufferIndex(lines),
        explorer(lines, navContext, boundary, params, goalRange, bufferIndex, 0),
        state(initialPos, RunningEffort(), 0.0, 0.0),
        bank(benchConfig) {}
};

struct StaticTransition {
  KSId motionId;
  const KeyedSequence* ks = nullptr;
  CursorPos endpoint;
};

struct CountedTransition {
  KSId motionId;
  const KeyedSequence* ks = nullptr;
  int count = 0;
  CursorPos endpoint;
  double extraPenalty = 0.0;
};

struct FMotionTransition {
  KeyedSequence motion;
  int newCol = 0;
};

enum class DispatchScenario {
  CodeLikeTailRange,
  ProseTailRange,
};

static vector<DispatchExploreCase> makeDispatchExploreCases(BufferShape shape,
                                                            bool useDirectionalPruning) {
  auto& seedMgr = SeedManager::instance();
  vector<DispatchExploreCase> cases;
  cases.reserve(DEFAULT_SEED_COUNT);
  auto params = NavOptimizerRangeParams{}.withDirectionalPruning(useDirectionalPruning);
  for (int i = 0; i < DEFAULT_SEED_COUNT; ++i) {
    RandomGen::seed(seedMgr.getSeed(i));
    cases.emplace_back(generateBuffer(20, 30, shape), params);
  }
  return cases;
}

static vector<DispatchExploreCase>& getDispatchExploreCases(DispatchScenario scenario,
                                                            bool useDirectionalPruning) {
  static vector<DispatchExploreCase> codeLikePruned =
      makeDispatchExploreCases(BufferShape::CodeLike, true);
  static vector<DispatchExploreCase> codeLikeNoPruning =
      makeDispatchExploreCases(BufferShape::CodeLike, false);
  static vector<DispatchExploreCase> prosePruned =
      makeDispatchExploreCases(BufferShape::Prose, true);
  static vector<DispatchExploreCase> proseNoPruning =
      makeDispatchExploreCases(BufferShape::Prose, false);

  switch (scenario) {
    case DispatchScenario::CodeLikeTailRange:
      return useDirectionalPruning ? codeLikePruned : codeLikeNoPruning;
    case DispatchScenario::ProseTailRange:
      return useDirectionalPruning ? prosePruned : proseNoPruning;
  }
  return codeLikePruned;
}

static void runWithParams(const BenchmarkSetup& cfg,
                          const NavOptimizerParams& params,
                          NavSearchStats& outStats) {
  NavOptimizer opt(benchConfig);
  auto result = opt.optimize(cfg.lines, cfg.firstPos, cfg.lastPos,
                             params, "", cfg.boundary);
  accumulateStats(outStats, result.getStats());
}

static void runRangeWithParams(const RangeBenchmarkSetup& cfg,
                               const NavOptimizerRangeParams& params,
                               NavSearchStats& outStats) {
  NavOptimizer opt(benchConfig);
  auto result = opt.optimizeToRange(
      cfg.lines, cfg.initialPos,
      toMotionInterval(cfg.lines, CharRange(cfg.rangeBegin, cfg.rangeEnd)),
      params, "", cfg.boundary);
  accumulateStats(outStats, result.getStats());
}

// =============================================================================
// Benchmark Functions
// =============================================================================

static void BM_MotionBufferSize(benchmark::State& state, bool useB) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  NavSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(numLines, 30));
    auto params = useB ? navParamsB() : navParamsA();
    runWithParams(setup, params, totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_MotionLineLength(benchmark::State& state, bool useB) {
  int avgLen = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  NavSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(20, avgLen));
    auto params = useB ? navParamsB() : navParamsA();
    runWithParams(setup, params, totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_MotionBufferShape(benchmark::State& state, bool useB, BufferShape shape) {
  auto& seedMgr = SeedManager::instance();
  NavSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = BenchmarkSetup(generateBuffer(20, 30, shape));
    auto params = useB ? navParamsB() : navParamsA();
    runWithParams(setup, params, totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_MotionRangeBufferLines(benchmark::State& state, bool useB) {
  int numLines = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  NavSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = RangeBenchmarkSetup(generateBuffer(numLines, 30));
    auto params = useB ? rangeParamsB() : rangeParamsA();
    runRangeWithParams(setup, params, totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

static void BM_MotionRangeResultSize(benchmark::State& state, bool useB,
                                      int rangeChars, int rangeLines) {
  auto& seedMgr = SeedManager::instance();
  NavSearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto setup = RangeBenchmarkSetup(generateBuffer(20, 30), rangeChars, rangeLines);
    auto params = useB ? rangeParamsB() : rangeParamsA();
    runRangeWithParams(setup, params, totalStats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

template<bool UseFixedBuffer>
static void BM_MotionExploreDispatch(benchmark::State& state,
                                     DispatchScenario scenario,
                                     bool useDirectionalPruning) {
  auto& cases = getDispatchExploreCases(scenario, useDirectionalPruning);
  constexpr size_t MAX_STATIC_TRANSITIONS = 128;
  constexpr size_t MAX_COUNTED_TRANSITIONS = 128;
  constexpr size_t MAX_FMOTION_TRANSITIONS = 32;

  size_t totalTransitions = 0;
  size_t maxStaticSeen = 0;
  size_t maxCountedSeen = 0;
  size_t maxFMotionSeen = 0;
  int caseIndex = 0;

  for (auto _ : state) {
    auto& c = cases[caseIndex % static_cast<int>(cases.size())];
    caseIndex++;

    NavPriorityQueue pq;
    unordered_map<Pos, double> costMap;
    const double maxEffort = numeric_limits<double>::max();

    auto isInGoalRange = [&](CursorPos pos) {
      return c.goalRange.containsPos(pos);
    };
    auto scoreState = [&](CursorPos pos, double effort) {
      double distance = NavHeuristic::distanceToRange(c.goalRange, pos);
      return c.params.effortWeight * effort + c.params.distanceWeight * distance;
    };

    size_t emitted = 0;
    if constexpr (!UseFixedBuffer) {
      auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
        NavState next = c.state.afterMotion(ks, c.bank[motionId], endpoint, benchConfig, scoreState);
        emitted++;
        Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
      };
      auto onCounted = [&](KSId motionId, const KeyedSequence& ks, int count,
                           CursorPos endpoint, double extraPenalty) {
        NavState next = c.state.afterCountedMotion(
            ks, count, endpoint, benchConfig, extraPenalty, scoreState);
        emitted++;
        Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
      };
      auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
        NavState next = c.state.afterFMotion(motion, newCol, benchConfig, scoreState);
        emitted++;
        Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
      };

      if (c.params.useDirectionalPruning) {
        c.explorer.exploreDirectionalStandardMotions(c.state, onStatic);
      } else {
        c.explorer.exploreAllStandardMotions(c.state, onStatic);
      }
      c.explorer.exploreCountedMotions(c.state, onCounted, onFMotion);
    } else {
      array<StaticTransition, MAX_STATIC_TRANSITIONS> staticTransitions;
      array<CountedTransition, MAX_COUNTED_TRANSITIONS> countedTransitions;
      array<FMotionTransition, MAX_FMOTION_TRANSITIONS> fMotionTransitions;
      size_t staticCount = 0;
      size_t countedCount = 0;
      size_t fMotionCount = 0;
      bool overflow = false;

      auto collectStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
        if (staticCount >= staticTransitions.size()) {
          overflow = true;
          return;
        }
        staticTransitions[staticCount++] = {motionId, &ks, endpoint};
      };
      auto collectCounted = [&](KSId motionId, const KeyedSequence& ks, int count,
                                CursorPos endpoint, double extraPenalty) {
        if (countedCount >= countedTransitions.size()) {
          overflow = true;
          return;
        }
        countedTransitions[countedCount++] = {motionId, &ks, count, endpoint, extraPenalty};
      };
      auto collectFMotion = [&](const KeyedSequence& motion, int newCol) {
        if (fMotionCount >= fMotionTransitions.size()) {
          overflow = true;
          return;
        }
        fMotionTransitions[fMotionCount++] = {motion, newCol};
      };

      if (c.params.useDirectionalPruning) {
        c.explorer.exploreDirectionalStandardMotions(c.state, collectStatic);
      } else {
        c.explorer.exploreAllStandardMotions(c.state, collectStatic);
      }
      c.explorer.exploreCountedMotions(c.state, collectCounted, collectFMotion);

      if (overflow) {
        state.SkipWithError("fixed transition buffer overflow");
        break;
      }

      maxStaticSeen = max(maxStaticSeen, staticCount);
      maxCountedSeen = max(maxCountedSeen, countedCount);
      maxFMotionSeen = max(maxFMotionSeen, fMotionCount);

      for (size_t i = 0; i < staticCount; ++i) {
        const auto& transition = staticTransitions[i];
        NavState next = c.state.afterMotion(
            *transition.ks, c.bank[transition.motionId], transition.endpoint, benchConfig, scoreState);
        emitted++;
        Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
      }
      for (size_t i = 0; i < countedCount; ++i) {
        const auto& transition = countedTransitions[i];
        NavState next = c.state.afterCountedMotion(
            *transition.ks, transition.count, transition.endpoint, benchConfig,
            transition.extraPenalty, scoreState);
        emitted++;
        Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
      }
      for (size_t i = 0; i < fMotionCount; ++i) {
        const auto& transition = fMotionTransitions[i];
        NavState next = c.state.afterFMotion(
            transition.motion, transition.newCol, benchConfig, scoreState);
        emitted++;
        Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
      }
    }

    totalTransitions += emitted;
    benchmark::DoNotOptimize(pq.size());
    benchmark::DoNotOptimize(costMap.size());
    benchmark::ClobberMemory();
  }

  state.counters["Transitions"] = benchmark::Counter(
      static_cast<double>(totalTransitions), benchmark::Counter::kAvgIterations);
  if constexpr (UseFixedBuffer) {
    state.counters["MaxStatic"] = static_cast<double>(maxStaticSeen);
    state.counters["MaxCounted"] = static_cast<double>(maxCountedSeen);
    state.counters["MaxFMotion"] = static_cast<double>(maxFMotionSeen);
  }
}

// =============================================================================
// Registration
// =============================================================================

static void registerArgBenchmark(const string& name, void(*fn)(benchmark::State&, bool),
                                 const vector<int>& args) {
  auto* a = benchmark::RegisterBenchmark(
      ENABLE_COMPARISON ? name + "/Standard" : name, fn, false);
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
  registerArgBenchmark("MotionOpt/BufferSize", BM_MotionBufferSize,
                       {1, 5, 10, 15, 20, 30});

  // LineLength
  registerArgBenchmark("MotionOpt/LineLength", BM_MotionLineLength,
                       {10, 20, 40, 60, 80});

  // BufferShape
  for (const auto& [name, shape] : vector<pair<string, BufferShape>>{
           {"Uniform", BufferShape::Uniform},
           {"Prose", BufferShape::Prose},
           {"CodeLike", BufferShape::CodeLike}}) {
    string benchName = "MotionOpt/BufferShape/" + name;
    auto* a = benchmark::RegisterBenchmark(
        ENABLE_COMPARISON ? benchName + "/Standard" : benchName, BM_MotionBufferShape, false, shape);
    a->Iterations(DEFAULT_SEED_COUNT);
    if (ENABLE_COMPARISON) {
      auto* b = benchmark::RegisterBenchmark(
          benchName + "/NoPruning", BM_MotionBufferShape, true, shape);
      b->Iterations(DEFAULT_SEED_COUNT);
    }
  }

  // RangeBufferLines
  registerArgBenchmark("MotionOpt/RangeBufferLines", BM_MotionRangeBufferLines,
                       {5, 10, 20, 30, 40});

  // RangeResultSize
  for (const auto& [label, chars, rangeLines] : vector<tuple<string, int, int>>{
           {"1col", 1, 1},
           {"3cols", 3, 1},
           {"6cols", 6, 1},
           {"10cols", 10, 1},
           {"30_2ln", 30, 2}}) {
    string benchName = "MotionOpt/RangeResultSize/" + label;
    auto* a = benchmark::RegisterBenchmark(
        ENABLE_COMPARISON ? benchName + "/Standard" : benchName,
        BM_MotionRangeResultSize, false, chars, rangeLines);
    a->Iterations(DEFAULT_SEED_COUNT);
    if (ENABLE_COMPARISON) {
      auto* b = benchmark::RegisterBenchmark(
          benchName + "/NoPruning",
          BM_MotionRangeResultSize, true, chars, rangeLines);
      b->Iterations(DEFAULT_SEED_COUNT);
    }
  }

  auto registerDispatchPair = [](const string& name,
                                 DispatchScenario scenario,
                                 bool useDirectionalPruning) {
    benchmark::RegisterBenchmark(
        name + "/Direct", BM_MotionExploreDispatch<false>, scenario, useDirectionalPruning);
    benchmark::RegisterBenchmark(
        name + "/FixedBuffer", BM_MotionExploreDispatch<true>, scenario, useDirectionalPruning);
  };

  registerDispatchPair("MotionOpt/ExploreDispatch/CodeLike/Pruned",
                       DispatchScenario::CodeLikeTailRange, true);
  registerDispatchPair("MotionOpt/ExploreDispatch/CodeLike/NoPruning",
                       DispatchScenario::CodeLikeTailRange, false);
  registerDispatchPair("MotionOpt/ExploreDispatch/Prose/Pruned",
                       DispatchScenario::ProseTailRange, true);
  registerDispatchPair("MotionOpt/ExploreDispatch/Prose/NoPruning",
                       DispatchScenario::ProseTailRange, false);

  return 0;
}();

} // namespace
