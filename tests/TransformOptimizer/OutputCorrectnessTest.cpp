// tests/TransformOptimizer/OutputCorrectnessTest.cpp
//
// Generated property tests for TransformOptimizer result validity.
// Each case replays a bounded set of emitted results against Neovim.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TransformOptimizerOutputCorrectness.*"

#include <gtest/gtest.h>
#include <memory>

#include "Boundary/TransformBoundary.h"
#include "Types/CursorPos.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Utils/EditTestGenerators.h"
#include "Types/Lines.h"
#include "Utils/GeneratedProperty.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OptimizerResultChecks.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

class TransformOptimizerOutputCorrectness : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static const int CNT = 30;
  static const int REDUCED_CNT = 5;  // Reduced - embedded optimization is slow (~500ms each)
  static constexpr size_t MAX_RESULTS_PER_START_TO_REPLAY = 3;
  Config config = Config::uniform();
  TransformOptimizerParams params{};
  TransformOptimizer opt{config};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  TransformResult pureDeletionResult(const Lines& initialLines, TransformBoundary boundary) {
    return opt.optimizePureDeletion(initialLines, boundary, params);
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
        *oracle, bucket, initial, initialPos, goal,
        MAX_RESULTS_PER_START_TO_REPLAY, context);
  }
};

unique_ptr<NeovimOracle> TransformOptimizerOutputCorrectness::oracle;

// =============================================================================
// Deletion generated properties
// =============================================================================

// Single-line embedded: PREFIX | EDIT_REGION | SUFFIX on one line
// Boundary checking works correctly since positions don't shift across lines
TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_SingleLineEmbeddedTopResultsReplay) {
  GeneratedProperty::check({"Transform single-line embedded replay", 42, CNT}, [&](int) {
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

// Multi-line full buffer deletion (no embedding - tests multi-line sequences work)
TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_MultiLineFullBufferTopResultsReplay) {
  GeneratedProperty::check({"Transform multi-line full buffer replay", 43, CNT}, [&](int) {
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
          bucket, source, pos, Lines{""},
          "multi-line full buffer", checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  });
}

TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_SameLengthReplacementTopResultsReplay) {
  GeneratedProperty::check({"Transform same-length replacement replay", 44, CNT}, [&](int) {
    int wordLen = RandomGen::range(5, 9);
    string original = randomWord(wordLen);
    string replacement = randomWord(wordLen);
    if (original == replacement) return;

    Lines source = {original};
    Lines goal = {replacement};
    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt.optimizeTransform(source, goal, boundary, params);

    const auto& bucket0 = res.getResults()[0];
    int checkedStarts = 0;
    expectEmittedTopResultsReachGoal(
        bucket0, source, CursorPos(0, 0), goal,
        "same-length replacement", checkedStarts);
    EXPECT_GT(checkedStarts, 0);
  });
}

// Multi-line embedded: PREFIX on first line, SUFFIX on last line
// Tests that cursor positions are consistent across line boundaries with embedding.
// With full prefix/suffix support, effectiveLines matches fullBuffer exactly,
// eliminating cursor position divergence after multi-line deletions.
TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_MultiLineEmbeddedTopResultsReplay) {
  GeneratedProperty::check({"Transform multi-line embedded replay", 45, REDUCED_CNT}, [&](int) {
    auto test = generateRandomMultiLineEmbedded();
    TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
    Lines expected{test.expectedAfterDeletion()};
    int checkedStarts = 0;

    // Test a subset of positions (every 4th to reduce test time)
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

// =============================================================================
// Change Tests (non-empty goalLines, oracle-verified typing path)
// =============================================================================

// Single-line change: generated initial and goal content, full-buffer boundary
TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_SingleLineChangeTopResultsReplay) {
  GeneratedProperty::check({"Transform single-line change replay", 50, CNT}, [&](int) {
    int initLen = RandomGen::range(5, 8);
    int goalLen = RandomGen::range(3, 7);
    Lines source = {randomLine(initLen)};
    Lines goal = {randomLine(goalLen)};
    if (source == goal) return;

    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt.optimizeTransform(source, goal, boundary, params);
    int checkedStarts = 0;

    for (size_t i = 0; i < res.resultCount(); i++) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
      expectEmittedTopResultsReachGoal(
          bucket, source, pos, goal,
          "single-line change", checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  });
}

// Multi-line change: different source/goal content, full-buffer boundary
TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_MultiLineFullBufferChangeTopResultsReplay) {
  const int NUM_ITERATIONS = 3;  // Change path is expensive (~1s/iter at 50k node cap)
  GeneratedProperty::check({"Transform multi-line full-buffer change replay", 51, NUM_ITERATIONS}, [&](int) {
    int numLines = RandomGen::range(2, 3);
    Lines source = randomLines(numLines, 4, 8);
    Lines goal = randomLines(numLines, 4, 8);
    if (source == goal) return;

    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt.optimizeTransform(source, goal, boundary, params);
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

// Multi-line embedded change: prefix/suffix around edit region with different goal content
TEST_F(TransformOptimizerOutputCorrectness, GeneratedProperty_MultiLineEmbeddedChangeTopResultsReplay) {
  GeneratedProperty::check({"Transform multi-line embedded change replay", 52, REDUCED_CNT}, [&](int) {
    auto test = generateRandomMultiLineEmbedded();

    // Generate goal lines: same count as editRegion, with possible indentation
    int numGoalLines = static_cast<int>(test.editRegion.size());
    Lines goal = randomLines(numGoalLines, 4, 8);

    // Skip if same as edit region (unlikely but possible)
    if (goal == test.editRegion) return;

    TransformResult res = opt.optimizeTransform(test.editRegion, goal, test.makeBoundary(), params);

    // Build expected full buffer: prefix + goal + suffix
    Lines expectedFull;
    for (int i = 0; i < numGoalLines; i++) {
      string line;
      if (i == 0) line += test.prefix;
      line += goal[i];
      if (i == numGoalLines - 1) line += test.suffix;
      expectedFull.push_back(line);
    }
    int checkedStarts = 0;

    // Test a subset of positions (every 4th to reduce test time)
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
