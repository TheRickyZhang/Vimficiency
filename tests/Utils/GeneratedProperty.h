#pragma once

#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include "Utils/RandomGeneration.h"

namespace GeneratedProperty {

struct Spec {
  std::string_view name;
  uint32_t seed;
  int iterations;
};

template <typename Fn>
void check(const Spec& spec, Fn&& fn) {
  RandomGen::seed(spec.seed);
  for (int iter = 0; iter < spec.iterations; iter++) {
    SCOPED_TRACE(
        ::testing::Message() << spec.name
                             << " seed=" << spec.seed
                             << " case=" << iter);
    fn(iter);
  }
}

}  // namespace GeneratedProperty
