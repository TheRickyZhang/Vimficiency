// tests/Benchmarks/CompositionOptimizerBench.cpp
//
// Performance benchmarks for CompositionOptimizer using Google Benchmark.
// Each category varies one parameter against consistent defaults.
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

namespace {

// =============================================================================
// Defaults — each benchmark overrides exactly one of these
// =============================================================================

constexpr int DEFAULT_LINES     = 15;
constexpr int DEFAULT_AVG_LEN   = 20;
constexpr int DEFAULT_EDIT_COUNT = 5;
constexpr BufferShape DEFAULT_SHAPE = BufferShape::CodeLike;

static Config benchConfig = Config::uniform();

// =============================================================================
// Shared Setup Helper
// =============================================================================

struct CompBenchSetup {
  Lines initial;
  Lines goal;
};

// Generate initial buffer, then produce goal by replacing content on
// evenly-spaced edit lines with random words of similar length.
static CompBenchSetup makeDefaultSetup(int numLines, int avgLen, int editCount,
                                        BufferShape shape = DEFAULT_SHAPE) {
  Lines initial = generateBuffer(numLines, avgLen, shape);
  Lines goal = initial;
  for (int e = 0; e < editCount; e++) {
    int line = editCount <= 1 ? numLines / 2
                              : e * (numLines - 1) / max(1, editCount - 1);
    int len = max(1, static_cast<int>(initial[line].size()));
    goal[line] = randomWord(len);
    if (goal[line] == initial[line]) goal[line] = "changed";
  }
  return {initial, goal};
}

// =============================================================================
// Benchmark Functions
// =============================================================================

// --- EditCount: varies number of separate edits ---
static void BM_CompEditCount(benchmark::State& state) {
  int editCount = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto [initial, goal] = makeDefaultSetup(DEFAULT_LINES, DEFAULT_AVG_LEN, editCount);
    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    accumulateStats(totalStats, result.stats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// --- EditSize: varies how much each edit changes ---
enum class EditSizeType { Small, Medium, Large, MultiLine };

static void BM_CompEditSize(benchmark::State& state, EditSizeType sizeType) {
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines initial = generateBuffer(DEFAULT_LINES, DEFAULT_AVG_LEN, DEFAULT_SHAPE);
    Lines goal = initial;

    for (int e = 0; e < DEFAULT_EDIT_COUNT; e++) {
      int goalSize = static_cast<int>(goal.size());
      int line = e * (goalSize - 1) / max(1, DEFAULT_EDIT_COUNT - 1);
      line = min(line, goalSize - 1);  // Clamp after MultiLine shrinks goal
      int lineLen = max(1, static_cast<int>(initial[min(line, static_cast<int>(initial.size()) - 1)].size()));

      switch (sizeType) {
        case EditSizeType::Small: {
          // Replace ~5 chars at a random offset
          string modified = initial[line];
          int replaceLen = min(5, lineLen);
          int offset = RandomGen::range(0, max(0, lineLen - replaceLen));
          for (int i = offset; i < offset + replaceLen && i < lineLen; i++)
            modified[i] = RandomGen::pick(CharPools::LETTERS);
          goal[line] = modified;
          if (goal[line] == initial[line]) goal[line][offset] = 'z';
          break;
        }
        case EditSizeType::Medium: {
          // Replace second half of line (~10 chars)
          string modified = initial[line];
          int start = lineLen / 2;
          for (int i = start; i < lineLen; i++)
            modified[i] = RandomGen::pick(CharPools::LETTERS);
          goal[line] = modified;
          if (goal[line] == initial[line]) goal[line][start] = 'z';
          break;
        }
        case EditSizeType::Large:
          // Replace entire line
          goal[line] = randomWord(lineLen);
          if (goal[line] == initial[line]) goal[line] = "changed";
          break;
        case EditSizeType::MultiLine: {
          // Merge edit line with next line, replace with ~50 char content
          if (line + 1 < static_cast<int>(goal.size())) {
            goal[line] = randomWord(DEFAULT_AVG_LEN * 5 / 2);
            goal.erase(goal.begin() + line + 1);
          } else {
            goal[line] = randomWord(DEFAULT_AVG_LEN * 5 / 2);
          }
          break;
        }
      }
    }
    if (goal == initial) goal[0] = "changed";

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    accumulateStats(totalStats, result.stats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// --- DeletionBalance: varies deletion vs insertion ratio ---
enum class BalanceType { PureDel, Shrink, Balanced, Grow, PureIns };

static void BM_CompDeletionBalance(benchmark::State& state, BalanceType balType) {
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    Lines initial = generateBuffer(DEFAULT_LINES, DEFAULT_AVG_LEN, DEFAULT_SHAPE);
    Lines goal = initial;

    // Collect edit lines (in reverse order for safe erasure)
    vector<int> editLines;
    for (int e = 0; e < DEFAULT_EDIT_COUNT; e++) {
      int line = e * (DEFAULT_LINES - 1) / max(1, DEFAULT_EDIT_COUNT - 1);
      editLines.push_back(line);
    }

    switch (balType) {
      case BalanceType::PureDel:
        // Remove edit lines entirely (reverse order to preserve indices)
        for (int i = static_cast<int>(editLines.size()) - 1; i >= 0; i--)
          goal.erase(goal.begin() + editLines[i]);
        if (goal.empty()) goal.push_back("");
        break;
      case BalanceType::Shrink:
        // Replace edit lines with short words
        for (int line : editLines)
          goal[line] = randomWord(5);
        break;
      case BalanceType::Balanced:
        // Replace with similar-length content (default)
        for (int line : editLines) {
          int len = max(1, static_cast<int>(initial[line].size()));
          goal[line] = randomWord(len);
          if (goal[line] == initial[line]) goal[line] = "changed";
        }
        break;
      case BalanceType::Grow:
        // Replace edit lines with long strings
        for (int line : editLines)
          goal[line] = randomWord(DEFAULT_AVG_LEN * 5 / 2);
        break;
      case BalanceType::PureIns:
        // Insert new lines after each edit line (reverse order to preserve indices)
        for (int i = static_cast<int>(editLines.size()) - 1; i >= 0; i--)
          goal.insert(goal.begin() + editLines[i] + 1,
                      randomWord(DEFAULT_AVG_LEN));
        break;
    }
    if (goal == initial) goal[0] = "changed";

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    accumulateStats(totalStats, result.stats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// --- CharDist: varies character composition of the buffer ---
enum class CharDistType { Alpha, MixedSymbol, HighSpace, Prose };

static void BM_CompCharDist(benchmark::State& state, CharDistType charType) {
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));

    Lines initial;
    initial.reserve(DEFAULT_LINES);
    for (int i = 0; i < DEFAULT_LINES; i++) {
      int len = RandomGen::range(DEFAULT_AVG_LEN / 2, DEFAULT_AVG_LEN * 3 / 2);
      switch (charType) {
        case CharDistType::Alpha:
          initial.push_back(randomAlphaLine(len));
          break;
        case CharDistType::MixedSymbol:
          initial.push_back(randomLine(len));  // 24% symbols
          break;
        case CharDistType::HighSpace:
          initial.push_back(randomHighSpaceLine(len));
          break;
        case CharDistType::Prose:
          initial.push_back(randomProseLine(len));
          break;
      }
    }

    // Apply default edits
    Lines goal = initial;
    for (int e = 0; e < DEFAULT_EDIT_COUNT; e++) {
      int line = e * (DEFAULT_LINES - 1) / max(1, DEFAULT_EDIT_COUNT - 1);
      int len = max(1, static_cast<int>(initial[line].size()));
      goal[line] = randomWord(len);
      if (goal[line] == initial[line]) goal[line] = "changed";
    }

    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    accumulateStats(totalStats, result.stats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// --- BufferSize: varies total buffer size ---
static void BM_CompBufferSize(benchmark::State& state) {
  int numLines = static_cast<int>(state.range(0));
  int editCount = min(DEFAULT_EDIT_COUNT, numLines);
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto [initial, goal] = makeDefaultSetup(numLines, DEFAULT_AVG_LEN, editCount);
    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    accumulateStats(totalStats, result.stats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// --- LineLength: varies average line length ---
static void BM_CompLineLength(benchmark::State& state) {
  int avgLen = static_cast<int>(state.range(0));
  auto& seedMgr = SeedManager::instance();
  SearchStats totalStats;
  int iter = 0;
  for (auto _ : state) {
    RandomGen::seed(seedMgr.getSeed(iter % DEFAULT_SEED_COUNT));
    auto [initial, goal] = makeDefaultSetup(DEFAULT_LINES, avgLen, DEFAULT_EDIT_COUNT);
    CompositionOptimizer opt(benchConfig);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
    accumulateStats(totalStats, result.stats);
    iter++;
  }
  setSearchCounters(state, totalStats);
}

// =============================================================================
// Registration Helpers
// =============================================================================

static void registerArgBenchmark(const string& name, void(*fn)(benchmark::State&),
                                  const vector<int>& args) {
  auto* b = benchmark::RegisterBenchmark(name, fn);
  for (int v : args) b->Arg(v);
  b->Iterations(DEFAULT_SEED_COUNT);
}

// =============================================================================
// Registration
// =============================================================================

static int registerCompositionBenchmarks = []() {
  // EditCount
  registerArgBenchmark("CompositionOpt/EditCount", BM_CompEditCount,
                       {1, 2, 5, 8});

  // EditSize
  for (const auto& [name, type] : vector<pair<string, EditSizeType>>{
           {"Small", EditSizeType::Small},
           {"Medium", EditSizeType::Medium},
           {"Large", EditSizeType::Large},
           {"MultiLine", EditSizeType::MultiLine}}) {
    auto* b = benchmark::RegisterBenchmark(
        "CompositionOpt/EditSize/" + name, BM_CompEditSize, type);
    b->Iterations(DEFAULT_SEED_COUNT);
  }

  // DeletionBalance
  for (const auto& [name, type] : vector<pair<string, BalanceType>>{
           {"PureDel", BalanceType::PureDel},
           {"Shrink", BalanceType::Shrink},
           {"Balanced", BalanceType::Balanced},
           {"Grow", BalanceType::Grow},
           {"PureIns", BalanceType::PureIns}}) {
    auto* b = benchmark::RegisterBenchmark(
        "CompositionOpt/DeletionBalance/" + name, BM_CompDeletionBalance, type);
    b->Iterations(DEFAULT_SEED_COUNT);
  }

  // CharDist
  for (const auto& [name, type] : vector<pair<string, CharDistType>>{
           {"Alpha", CharDistType::Alpha},
           {"MixedSymbol", CharDistType::MixedSymbol},
           {"HighSpace", CharDistType::HighSpace},
           {"Prose", CharDistType::Prose}}) {
    auto* b = benchmark::RegisterBenchmark(
        "CompositionOpt/CharDist/" + name, BM_CompCharDist, type);
    b->Iterations(DEFAULT_SEED_COUNT);
  }

  // BufferSize
  registerArgBenchmark("CompositionOpt/BufferSize", BM_CompBufferSize,
                       {5, 10, 20, 40});

  // LineLength
  registerArgBenchmark("CompositionOpt/LineLength", BM_CompLineLength,
                       {10, 20, 40, 60});

  return 0;
}();

} // namespace
