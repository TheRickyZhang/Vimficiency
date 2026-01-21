// tests/Optimizer/EditOptimizerStressTests.cpp
//
// Stress tests for EditOptimizer using NeovimOracle as ground truth.

#include <gtest/gtest.h>
#include <memory>
#include <random>

#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;


class EditOptimizerStressTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(30, 1e4, 1.0, 2.0));
  }
};

unique_ptr<NeovimOracle> EditOptimizerStressTest::oracle;

// =============================================================================
// Pure Deletion Tests - delete embedded regions to empty
// =============================================================================

// Single-line embedded: PREFIX | EDIT_REGION | SUFFIX on one line
// Boundary checking works correctly since positions don't shift across lines
TEST_F(EditOptimizerStressTest, PureDeletion_SingleLineEmbedded) {
  const int NUM_ITERATIONS = 50;
  mt19937 rng(42);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    auto test = generateRandomSingleLineEmbedded(rng);
    Lines target = {""};

    EditBoundary boundary = test.makeBoundary();
    EditOptimizer opt = makeOptimizer();
    EditResult res = opt.optimizeEdit(test.editRegion, target, boundary);

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
TEST_F(EditOptimizerStressTest, PureDeletion_MultiLineFullBuffer) {
  oracle->restart();
  const int NUM_ITERATIONS = 30;
  mt19937 rng(43);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int numLines = 2 + rng() % 2;  // 2-3 lines
    Lines source = randomLines(rng, numLines, 4, 8);
    Lines target = {""};

    int lastLine = static_cast<int>(source.size()) - 1;
    int lastCol = source[lastLine].empty() ? 0 : static_cast<int>(source[lastLine].size()) - 1;
    EditBoundary boundary(source, {0, 0}, {lastLine, lastCol});

    EditOptimizer opt = makeOptimizer();
    EditResult res = opt.optimizeEdit(source, target, boundary);

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

// =============================================================================
// Replacement Tests - same-length string replacement
// =============================================================================

TEST_F(EditOptimizerStressTest, DISABLED_Replacement_SameLength) {
  oracle->restart();
  const int NUM_ITERATIONS = 30;
  mt19937 rng(44);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int wordLen = 5 + (rng() % 5);
    string original = randomWord(rng, wordLen);
    string replacement = randomWord(rng, wordLen);
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
TEST_F(EditOptimizerStressTest, PureDeletion_MultiLineEmbedded) {
  oracle->restart();
  const int NUM_ITERATIONS = 50;
  mt19937 rng(45);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    auto test = generateRandomMultiLineEmbedded(rng);
    Lines target = {""};

    EditBoundary boundary = test.makeBoundary();
    EditOptimizer opt = makeOptimizer();
    EditResult res = opt.optimizeEdit(test.editRegion, target, boundary);

    string expected = test.expectedAfterDeletion();

    // Test a subset of positions (every 2nd to reduce test time)
    for (size_t i = 0; i < res.typeAllResults.size(); i += 2) {
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

