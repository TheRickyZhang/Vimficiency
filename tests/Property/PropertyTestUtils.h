#pragma once

#include <cstdint>

#include <gtest/gtest.h>

#include "Utils/RandomGeneration.h"

// Bridge for properties still using FuzzTest-generated integers as RandomGen
// seeds. Prefer direct domains for new or migrated properties.
template <typename Fn>
void runSeedDriverCases(uint32_t seed, int count, Fn&& fn) {
  RandomGen::seed(seed);
  for (int caseIndex = 0; caseIndex < count; caseIndex++) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
    fn();
  }
}
