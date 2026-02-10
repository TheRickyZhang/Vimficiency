// tests/Benchmarks/BenchmarkMain.cpp
//
// Custom main for Google Benchmark with SeedManager initialization.

#include <iostream>

#include <benchmark/benchmark.h>

#include "Utils/SeedManager.h"
#include "Benchmarks/BenchUtils.h"

int main(int argc, char** argv) {
  // Initialize SeedManager (reads VIMFICIENCY_SEED_MODE env var)
  auto& seedMgr = SeedManager::instance();

  std::cout << "Seed mode: " << getSeedModeDescription() << "\n";
  std::cout << "Seeds per benchmark: " << DEFAULT_SEED_COUNT << "\n\n";

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
