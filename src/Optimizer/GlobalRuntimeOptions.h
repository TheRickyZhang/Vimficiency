#pragma once

#include "Optimizer/CountPenalty.h"

struct GlobalRuntimeOptions {
  bool useCountPenaltyOverrides = false;
  CountPenaltyOverrideTable countPenaltyOverrides{};
};

inline GlobalRuntimeOptions& globalRuntimeOptions() {
  static GlobalRuntimeOptions options;
  return options;
}

template<CountClass C>
inline double runtimeCountPenalty(const CountPenaltyInput& in) {
  const auto& options = globalRuntimeOptions();
  if (options.useCountPenaltyOverrides) {
    return countPenalty<C>(in, options.countPenaltyOverrides);
  }
  return countPenalty<C>(in);
}
