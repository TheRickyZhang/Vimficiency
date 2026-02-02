// tests/EditOptimizer/OutputCorrectnessTest.cpp
//
// Random/stress tests for EditOptimizer output correctness.
// These tests use randomly generated buffers and verify results against Neovim.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="EditOptimizerOutputCorrectness.*"

#include <gtest/gtest.h>
#include <memory>

#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

class EditOptimizerOutputCorrectness : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  EditOptimizerParams params{};
  EditOptimizer opt{config};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> EditOptimizerOutputCorrectness::oracle;

// =============================================================================
// Stress Tests (random buffers, oracle-verified)
// =============================================================================

// Single-line embedded: PREFIX | EDIT_REGION | SUFFIX on one line
// Boundary checking works correctly since positions don't shift across lines
TEST_F(EditOptimizerOutputCorrectness, SingleLineEmbedded) {
  const int NUM_ITERATIONS = 50;
  RandomGen::seed(42);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    auto test = generateRandomSingleLineEmbedded();
    EditResult res = opt.optimizeEdit(test.editRegion, {""}, test.makeBoundary(), params);
    string expected = test.expectedAfterDeletion();

    for (size_t i = 0; i < res.typeAllResults.size(); i++) {
      const Result& r = res.typeAllResults[i];
      if (!r.isValid()) continue;

      Position editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      Position bufferPos = test.toFullBufferPos(editPos);

      total++;
      string seq = r.getSequenceString();
      auto result = oracle->simulate(test.fullBuffer, bufferPos.line, bufferPos.col, seq);

      if (result.lines.flatten() == expected) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " col=" << editPos.col << " seq='" << seq << "'\n"
               << "  FullBuffer: '" << test.fullBuffer[0] << "'\n"
               << "  EditRegion: '" << test.editRegion[0] << "'\n"
               << "  Expected: '" << expected << "'\n"
               << "  Got: '" << result.lines.flatten() << "'" << endl;
        }
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Multi-line full buffer deletion (no embedding - tests multi-line sequences work)
TEST_F(EditOptimizerOutputCorrectness, MultiLineFullBuffer) {
  oracle->restart();
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(43);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int numLines = RandomGen::range(2, 3);
    Lines source = randomLines(numLines, 4, 8);
    EditBoundary boundary(source, {0, 0}, source.lastPos());
    EditResult res = opt.optimizeEdit(source, {""}, boundary, params);

    for (size_t i = 0; i < res.typeAllResults.size(); i += 2) {
      const Result& r = res.typeAllResults[i];
      if (!r.isValid()) continue;

      Position pos = fromFlatIndex(static_cast<int>(i), source);

      total++;
      string seq = r.getSequenceString();
      auto nvim = oracle->simulate(source, pos.line, pos.col, seq);

      bool ok = (nvim.lines.size() == 1 && nvim.lines[0].empty()) || nvim.lines.empty();
      if (ok) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " pos=[" << pos.line << "," << pos.col
               << "] seq='" << seq << "'\n"
               << "  Source: " << source << "\n"
               << "  Result: " << nvim.lines << endl;
        }
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

TEST_F(EditOptimizerOutputCorrectness, Replacement_SameLength) {
  oracle->restart();
  const int NUM_ITERATIONS = 30;
  RandomGen::seed(44);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int wordLen = RandomGen::range(5, 9);
    string original = randomWord(wordLen);
    string replacement = randomWord(wordLen);
    if (original == replacement) continue;

    Lines source = {original};

    vector<Result> results;
    int lastPos = -1;
    tryReplacement(original, replacement, config, lastPos, results);
    if (results.empty()) continue;

    // Results[k] is designed to be applied from starting position k
    for (size_t k = 0; k < results.size(); k++) {
      const Result& r = results[k];
      if (!r.isValid()) continue;
      total++;

      string seq = r.getSequenceString();
      auto nvim = oracle->simulate(source, 0, static_cast<int>(k), seq);

      if (nvim.lines.size() == 1 && nvim.lines[0] == replacement) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " startCol=" << k << " seq='" << seq << "'\n"
               << "  Original: '" << original << "'\n"
               << "  Expected: '" << replacement << "'\n"
               << "  Got: " << nvim.lines << endl;
        }
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Multi-line embedded: PREFIX on first line, SUFFIX on last line
// Tests that cursor positions are consistent across line boundaries with embedding.
// With full prefix/suffix support, effectiveLines matches fullBuffer exactly,
// eliminating cursor position divergence after multi-line deletions.
TEST_F(EditOptimizerOutputCorrectness, MultiLineEmbedded) {
  oracle->restart();
  const int NUM_ITERATIONS = 8;  // Reduced - embedded optimization is slow (~500ms each)
  RandomGen::seed(45);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    auto test = generateRandomMultiLineEmbedded();
    EditResult res = opt.optimizeEdit(test.editRegion, {""}, test.makeBoundary(), params);
    string expected = test.expectedAfterDeletion();

    // Test a subset of positions (every 4th to reduce test time)
    for (size_t i = 0; i < res.typeAllResults.size(); i += 4) {
      const Result& r = res.typeAllResults[i];
      if (!r.isValid()) continue;

      Position editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      Position bufferPos = test.toFullBufferPos(editPos);

      total++;
      string seq = r.getSequenceString();
      auto nvim = oracle->simulate(test.fullBuffer, bufferPos.line, bufferPos.col, seq);

      if (nvim.lines.flatten() == expected) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " editPos=[" << editPos.line << "," << editPos.col
               << "] bufferPos=[" << bufferPos.line << "," << bufferPos.col << "] seq='" << seq << "'\n"
               << "  FullBuffer: " << test.fullBuffer << "\n"
               << "  EditRegion: " << test.editRegion << "\n"
               << "  Expected: '" << expected << "'\n"
               << "  Got: '" << nvim.lines.flatten() << "'" << endl;
        }
      }
    }
  }

  // With full prefix/suffix support, expect 100% pass rate
  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}
