#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Boundary/TransformBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OptimizerResultChecks.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

class TransformOptimizerGeneratedPropertyTest {
 public:
  void SingleLineEmbeddedTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      auto test = generateRandomSingleLineEmbedded();
      TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
      Lines expected{test.expectedAfterDeletion()};
      int checkedStarts = 0;

      for (size_t i = 0; i < res.resultCount(); i++) {
        const auto& bucket = res.getResults()[i];
        if (bucket.empty()) continue;

        CursorPos editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
        CursorPos bufferPos = test.toFullBufferPos(editPos);
        expectEmittedTopResultsReachGoal(
            bucket, test.fullBuffer, bufferPos, expected,
            "single-line embedded", checkedStarts);
      }
      EXPECT_GT(checkedStarts, 0);
    });
  }

  void MultiLineFullBufferTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int numLines = RandomGen::range(2, 3);
      Lines source = randomLines(numLines, 4, 8);
      TransformBoundary boundary(source, {0, 0}, source.endPos());
      TransformResult res = pureDeletionResult(source, boundary);
      int checkedStarts = 0;

      for (size_t i = 0; i < res.resultCount(); i += 2) {
        const auto& bucket = res.getResults()[i];
        if (bucket.empty()) continue;

        CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
        expectEmittedTopResultsReachGoal(
            bucket, source, pos, Lines{""}, "multi-line full buffer", checkedStarts);
      }
      EXPECT_GT(checkedStarts, 0);
    });
  }

  void SameLengthReplacementTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      int wordLen = RandomGen::range(5, 9);
      string original = randomWord(wordLen);
      string replacement = randomWord(wordLen);
      if (original == replacement) return;

      Lines source = {original};
      Lines goal = {replacement};
      TransformBoundary boundary(source, {0, 0}, source.endPos());
      TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);

      int checkedStarts = 0;
      expectEmittedTopResultsReachGoal(
          res.getResults()[0], source, CursorPos(0, 0), goal,
          "same-length replacement", checkedStarts);
      EXPECT_GT(checkedStarts, 0);
    });
  }

  void MultiLineEmbeddedTopResultsReplay(uint32_t seed) {
    runCases(seed, 5, [&] {
      auto test = generateRandomMultiLineEmbedded();
      TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
      Lines expected{test.expectedAfterDeletion()};
      int checkedStarts = 0;

      for (size_t i = 0; i < res.resultCount(); i += 4) {
        const auto& bucket = res.getResults()[i];
        if (bucket.empty()) continue;

        CursorPos editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
        CursorPos bufferPos = test.toFullBufferPos(editPos);
        expectEmittedTopResultsReachGoal(
            bucket, test.fullBuffer, bufferPos, expected,
            "multi-line embedded", checkedStarts);
      }
      EXPECT_GT(checkedStarts, 0);
    });
  }

  void SingleLineChangeTopResultsReplay(uint32_t seed) {
    runCases(seed, 30, [&] {
      Lines source = {randomLine(RandomGen::range(5, 8))};
      Lines goal = {randomLine(RandomGen::range(3, 7))};
      if (source == goal) return;

      TransformBoundary boundary(source, {0, 0}, source.endPos());
      TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);
      int checkedStarts = 0;

      for (size_t i = 0; i < res.resultCount(); i++) {
        const auto& bucket = res.getResults()[i];
        if (bucket.empty()) continue;

        CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
        expectEmittedTopResultsReachGoal(
            bucket, source, pos, goal, "single-line change", checkedStarts);
      }
      EXPECT_GT(checkedStarts, 0);
    });
  }

  void MultiLineFullBufferChangeTopResultsReplay(uint32_t seed) {
    runCases(seed, 3, [&] {
      int numLines = RandomGen::range(2, 3);
      Lines source = randomLines(numLines, 4, 8);
      Lines goal = randomLines(numLines, 4, 8);
      if (source == goal) return;

      TransformBoundary boundary(source, {0, 0}, source.endPos());
      TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);
      int checkedStarts = 0;

      for (size_t i = 0; i < res.resultCount(); i += 4) {
        const auto& bucket = res.getResults()[i];
        if (bucket.empty()) continue;

        CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
        expectEmittedTopResultsReachGoal(
            bucket, source, pos, goal,
            "multi-line full-buffer change", checkedStarts);
      }
      EXPECT_GT(checkedStarts, 0);
    });
  }

  void MultiLineEmbeddedChangeTopResultsReplay(uint32_t seed) {
    runCases(seed, 5, [&] {
      auto test = generateRandomMultiLineEmbedded();

      int numGoalLines = static_cast<int>(test.editRegion.size());
      Lines goal = randomLines(numGoalLines, 4, 8);
      if (goal == test.editRegion) return;

      TransformResult res =
          opt_.optimizeTransform(test.editRegion, goal, test.makeBoundary(), params_);

      Lines expectedFull;
      for (int i = 0; i < numGoalLines; i++) {
        string line;
        if (i == 0) line += test.prefix;
        line += goal[i];
        if (i == numGoalLines - 1) line += test.suffix;
        expectedFull.push_back(line);
      }

      int checkedStarts = 0;
      for (size_t i = 0; i < res.resultCount(); i += 4) {
        const auto& bucket = res.getResults()[i];
        if (bucket.empty()) continue;

        CursorPos editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
        CursorPos bufferPos = test.toFullBufferPos(editPos);
        expectEmittedTopResultsReachGoal(
            bucket, test.fullBuffer, bufferPos, expectedFull,
            "multi-line embedded change", checkedStarts);
      }
      EXPECT_GT(checkedStarts, 0);
    });
  }

 private:
  static constexpr size_t kMaxResultsPerStartToReplay = 3;

  Config config_ = Config::uniform();
  TransformOptimizerParams params_{};
  TransformOptimizer opt_{config_};
  NeovimOracle oracle_{};

  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }

  TransformResult pureDeletionResult(
      const Lines& initialLines, TransformBoundary boundary) {
    return opt_.optimizePureDeletion(initialLines, boundary, params_);
  }

  void expectEmittedTopResultsReachGoal(
      const vector<Result>& bucket,
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      const string& context,
      int& checkedStarts) {
    if (bucket.empty()) return;
    checkedStarts++;
    OptimizerResultChecks::expectTopResultsReplay(
        oracle_, bucket, initial, initialPos, goal,
        kMaxResultsPerStartToReplay, context);
  }
};

}  // namespace

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineEmbeddedTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({42});

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineFullBufferTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({43});

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SameLengthReplacementTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({44});

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineEmbeddedTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({45});

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineChangeTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({50});

FUZZ_TEST_F(
    TransformOptimizerGeneratedPropertyTest,
    MultiLineFullBufferChangeTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({51});

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineEmbeddedChangeTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({52});
