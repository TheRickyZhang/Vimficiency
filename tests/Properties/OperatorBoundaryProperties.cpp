#include <cstdint>
#include <memory>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Operator/TestHelpers.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

// Boundary-crossing prediction for operator + word motion and operator + text
// object. Property: when VimCore predicts a motion stays within the edit
// region, Neovim's actual execution must also stay within the edit region
// (i.e. prefix/suffix bytes are preserved). Failure means VimCore would let
// the optimizer schedule a deletion that mangles content outside the region.
class OperatorBoundaryGeneratedPropertyTest {
 public:
  void WordMotionBoundaryMatchesOracle_SingleLine(uint32_t seed) {
    runCases(seed, 20, [&] {
      auto test = generateRandomBuffer(1);
      for (const auto& motion : getAllWordMotions()) {
        EXPECT_TRUE(runRandomMotionTest(*oracle_, motion, test, /*verbose=*/true))
            << "word motion '" << motion.cmd << "' crossed without being predicted";
      }
    });
  }

  void WordMotionBoundaryMatchesOracle_MultiLine(uint32_t seed) {
    runCases(seed, 20, [&] {
      auto test = generateRandomBuffer(RandomGen::range(2, 5));
      for (const auto& motion : getAllWordMotions()) {
        EXPECT_TRUE(runRandomMotionTest(*oracle_, motion, test, /*verbose=*/true))
            << "word motion '" << motion.cmd << "' crossed without being predicted";
      }
    });
  }

  void TextObjectBoundaryMatchesOracle_SingleLine(uint32_t seed) {
    runCases(seed, 20, [&] {
      auto test = generateTextObjectBuffer(1);
      for (const auto& spec : getAllTextObjects()) {
        EXPECT_TRUE(runTextObjectTest(*oracle_, spec, test, /*verbose=*/true))
            << "text object '" << spec.cmd << "' crossed without being predicted";
      }
    });
  }

  void TextObjectBoundaryMatchesOracle_MultiLine(uint32_t seed) {
    runCases(seed, 20, [&] {
      auto test = generateTextObjectBuffer(RandomGen::range(2, 4));
      for (const auto& spec : getAllTextObjects()) {
        EXPECT_TRUE(runTextObjectTest(*oracle_, spec, test, /*verbose=*/true))
            << "text object '" << spec.cmd << "' crossed without being predicted";
      }
    });
  }

 private:
  unique_ptr<NeovimOracle> oracle_{make_unique<NeovimOracle>()};

  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }
};

}  // namespace

FUZZ_TEST_F(
    OperatorBoundaryGeneratedPropertyTest, WordMotionBoundaryMatchesOracle_SingleLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({4001});

FUZZ_TEST_F(
    OperatorBoundaryGeneratedPropertyTest, WordMotionBoundaryMatchesOracle_MultiLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({4002});

FUZZ_TEST_F(
    OperatorBoundaryGeneratedPropertyTest, TextObjectBoundaryMatchesOracle_SingleLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({4003});

FUZZ_TEST_F(
    OperatorBoundaryGeneratedPropertyTest, TextObjectBoundaryMatchesOracle_MultiLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({4004});
