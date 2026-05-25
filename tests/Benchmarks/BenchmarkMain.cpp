// tests/Benchmarks/BenchmarkMain.cpp
//
// Custom main for Google Benchmark with SeedManager initialization.

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <benchmark/benchmark.h>

#include "Utils/SeedManager.h"
#include "Benchmarks/BenchUtils.h"

int main(int argc, char** argv) {
  // Initialize SeedManager (reads VIMFY_SEED_MODE env var)
  auto& seedMgr = SeedManager::instance();
  if (std::getenv("VIMFY_SEED_MODE") == nullptr &&
      std::getenv("VIMFY_RANDOM_SEEDS") == nullptr &&
      std::getenv("VIMFICIENCY_RANDOM_SEEDS") == nullptr) {
    seedMgr.setFixedMode();
  }

  std::cout << "Seed mode: " << getSeedModeDescription() << "\n";
  std::cout << "Seeds per benchmark: " << DEFAULT_SEED_COUNT << "\n\n" << std::flush;

  // Default to milliseconds for time display (override with --benchmark_time_unit=ns)
  benchmark::SetDefaultTimeUnit(benchmark::kMillisecond);

  // Inject --benchmark_counters_tabular=true unless user explicitly set it
  bool hasTabular = false;
  bool hasMinTime = false;
  for (int i = 1; i < argc; i++) {
    if (std::string_view(argv[i]).find("benchmark_counters_tabular") != std::string_view::npos) {
      hasTabular = true;
    }
    if (std::string_view(argv[i]).find("benchmark_min_time") != std::string_view::npos) {
      hasMinTime = true;
    }
  }
  std::vector<char*> args(argv, argv + argc);
  static char tabularFlag[] = "--benchmark_counters_tabular=true";
  static std::string minTimeFlag =
      "--benchmark_min_time=" + std::to_string(DEFAULT_BENCH_MIN_TIME) + "s";
  if (!hasTabular) args.push_back(tabularFlag);
  if (!hasMinTime) args.push_back(minTimeFlag.data());
  int newArgc = static_cast<int>(args.size());
  benchmark::Initialize(&newArgc, args.data());
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
