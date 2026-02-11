// tests/Benchmarks/BenchmarkMain.cpp
//
// Custom main for Google Benchmark with SeedManager initialization.

#include <iostream>
#include <vector>

#include <benchmark/benchmark.h>

#include "Utils/SeedManager.h"
#include "Benchmarks/BenchUtils.h"

int main(int argc, char** argv) {
  // Initialize SeedManager (reads VIMFICIENCY_SEED_MODE env var)
  auto& seedMgr = SeedManager::instance();

  std::cout << "Seed mode: " << getSeedModeDescription() << "\n";
  std::cout << "Seeds per benchmark: " << DEFAULT_SEED_COUNT << "\n\n";

  // Default to seconds for time display (override with --benchmark_time_unit=ns)
  benchmark::SetDefaultTimeUnit(benchmark::kMillisecond);

  // Inject --benchmark_counters_tabular=true unless user explicitly set it
  bool hasTabular = false;
  for (int i = 1; i < argc; i++) {
    if (std::string_view(argv[i]).find("benchmark_counters_tabular") != std::string_view::npos) {
      hasTabular = true;
      break;
    }
  }
  std::vector<char*> args(argv, argv + argc);
  static char tabularFlag[] = "--benchmark_counters_tabular=true";
  if (!hasTabular) args.push_back(tabularFlag);
  int newArgc = static_cast<int>(args.size());
  benchmark::Initialize(&newArgc, args.data());
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
