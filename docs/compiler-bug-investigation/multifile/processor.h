#pragma once

#include "params.h"
#include <string>
#include <iostream>

// Simulates BenchUtils.h template
template<bool EnableComparison, typename SetupT, typename ParamsT, typename RunFn>
void runUnifiedBenchmark(const std::string& label, const SetupT& setup,
                         ParamsT paramsA, ParamsT paramsB, RunFn runFn) {
  if constexpr (EnableComparison) {
    auto resultA = runFn(setup, paramsA);
    auto resultB = runFn(setup, paramsB);
    std::cout << label << ": A.useFeature=" << paramsA.useFeature
              << " B.useFeature=" << paramsB.useFeature << std::endl;
  }
}
