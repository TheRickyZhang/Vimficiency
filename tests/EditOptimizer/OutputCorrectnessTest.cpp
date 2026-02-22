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
  static const int CNT = 30;
  static const int REDUCED_CNT = 5;  // Reduced - embedded optimization is slow (~500ms each)
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
  RandomGen::seed(42);
  int passed = 0, total = 0;

  for (int iter = 0; iter < CNT; iter++) {
    auto test = generateRandomSingleLineEmbedded();
    EditResult res = opt.optimizePureDeletion(test.editRegion, test.makeBoundary(), params).editResult;
    string expected = test.expectedAfterDeletion();

    for (size_t i = 0; i < res.resultCount(); i++) {
      const Result& r = res.getResults()[i];
      if (!r.isValid()) continue;

      Position editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      Position bufferPos = test.toFullBufferPos(editPos);

      total++;
      const auto& seq = r.getSequenceString();
      auto result = oracle->simulate(test.fullBuffer, bufferPos.line, bufferPos.col, seq.str());

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
  RandomGen::seed(43);
  int passed = 0, total = 0;

  for (int iter = 0; iter < CNT; iter++) {
    int numLines = RandomGen::range(2, 3);
    Lines source = randomLines(numLines, 4, 8);
    EditBoundary boundary(source, {0, 0}, source.endPos());
    EditResult res = opt.optimizePureDeletion(source, boundary, params).editResult;

    for (size_t i = 0; i < res.resultCount(); i += 2) {
      const Result& r = res.getResults()[i];
      if (!r.isValid()) continue;

      Position pos = fromFlatIndex(static_cast<int>(i), source);

      total++;
      const auto& seq = r.getSequenceString();
      auto nvim = oracle->simulate(source, pos.line, pos.col, seq.str());

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
  RandomGen::seed(44);
  int passed = 0, total = 0;

  for (int iter = 0; iter < CNT; iter++) {
    int wordLen = RandomGen::range(5, 9);
    string original = randomWord(wordLen);
    string replacement = randomWord(wordLen);
    if (original == replacement) continue;

    Lines source = {original};
    Lines goal = {replacement};
    EditBoundary boundary(source, {0, 0}, source.endPos());
    EditResult res = opt.optimizeEdit(source, goal, boundary, params);

    const Result& r = res.getResults()[0];
    if (!r.isValid()) continue;
    total++;

    const auto& seq = r.getSequenceString();
    auto nvim = oracle->simulate(source, 0, 0, seq.str());

    if (nvim.lines.size() == 1 && nvim.lines[0] == replacement) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Original: '" << original << "'\n"
             << "  Expected: '" << replacement << "'\n"
             << "  Got: " << nvim.lines << endl;
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
  RandomGen::seed(45);
  int passed = 0, total = 0;

  for (int iter = 0; iter < REDUCED_CNT; iter++) {
    auto test = generateRandomMultiLineEmbedded();
    EditResult res = opt.optimizePureDeletion(test.editRegion, test.makeBoundary(), params).editResult;
    string expected = test.expectedAfterDeletion();

    // Test a subset of positions (every 4th to reduce test time)
    for (size_t i = 0; i < res.resultCount(); i += 4) {
      const Result& r = res.getResults()[i];
      if (!r.isValid()) continue;

      Position editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      Position bufferPos = test.toFullBufferPos(editPos);

      total++;
      const auto& seq = r.getSequenceString();
      auto nvim = oracle->simulate(test.fullBuffer, bufferPos.line, bufferPos.col, seq.str());

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

// =============================================================================
// Change Tests (non-empty goalLines, oracle-verified typing path)
// =============================================================================

// Single-line change: random initial and goal content, full-buffer boundary
TEST_F(EditOptimizerOutputCorrectness, SingleLine_Change) {
  RandomGen::seed(50);
  int passed = 0, total = 0;

  for (int iter = 0; iter < CNT; iter++) {
    int initLen = RandomGen::range(5, 8);
    int goalLen = RandomGen::range(3, 7);
    Lines source = {randomLine(initLen)};
    Lines goal = {randomLine(goalLen)};
    if (source == goal) continue;

    EditBoundary boundary(source, {0, 0}, source.endPos());
    EditResult res = opt.optimizeEdit(source, goal, boundary, params);

    for (size_t i = 0; i < res.resultCount(); i++) {
      const Result& r = res.getResults()[i];
      if (!r.isValid()) continue;

      Position pos = fromFlatIndex(static_cast<int>(i), source);

      total++;
      const auto& seq = r.getSequenceString();
      auto nvim = oracle->simulate(source, pos.line, pos.col, seq.str());

      if (nvim.lines == goal) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " pos=[" << pos.line << "," << pos.col
               << "] seq='" << seq << "'\n"
               << "  Source: " << source << "\n"
               << "  Goal: " << goal << "\n"
               << "  Got: " << nvim.lines << endl;
        }
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Multi-line change: different source/goal content, full-buffer boundary
TEST_F(EditOptimizerOutputCorrectness, MultiLine_FullBufferChange) {
  const int NUM_ITERATIONS = 3;  // Change path is expensive (~1s/iter at 50k node cap)
  RandomGen::seed(51);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int numLines = RandomGen::range(2, 3);
    Lines source = randomLines(numLines, 4, 8);
    Lines goal = randomLines(numLines, 4, 8);
    if (source == goal) continue;

    EditBoundary boundary(source, {0, 0}, source.endPos());
    EditResult res = opt.optimizeEdit(source, goal, boundary, params);

    for (size_t i = 0; i < res.resultCount(); i += 4) {
      const Result& r = res.getResults()[i];
      if (!r.isValid()) continue;

      Position pos = fromFlatIndex(static_cast<int>(i), source);

      total++;
      const auto& seq = r.getSequenceString();
      auto nvim = oracle->simulate(source, pos.line, pos.col, seq.str());

      if (nvim.lines == goal) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " pos=[" << pos.line << "," << pos.col
               << "] seq='" << seq << "'\n"
               << "  Source: " << source << "\n"
               << "  Goal: " << goal << "\n"
               << "  Got: " << nvim.lines << endl;
        }
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Multi-line embedded change: prefix/suffix around edit region with different goal content
TEST_F(EditOptimizerOutputCorrectness, MultiLine_EmbeddedChange) {
  RandomGen::seed(52);
  int passed = 0, total = 0;

  for (int iter = 0; iter < REDUCED_CNT; iter++) {
    auto test = generateRandomMultiLineEmbedded();

    // Generate goal lines: same count as editRegion, with possible indentation
    int numGoalLines = static_cast<int>(test.editRegion.size());
    Lines goal = randomLines(numGoalLines, 4, 8);

    // Skip if same as edit region (unlikely but possible)
    if (goal == test.editRegion) continue;

    EditResult res = opt.optimizeEdit(test.editRegion, goal, test.makeBoundary(), params);

    // Build expected full buffer: prefix + goal + suffix
    Lines expectedFull;
    for (int i = 0; i < numGoalLines; i++) {
      string line;
      if (i == 0) line += test.prefix;
      line += goal[i];
      if (i == numGoalLines - 1) line += test.suffix;
      expectedFull.push_back(line);
    }
    string expected = expectedFull.flatten();

    // Test a subset of positions (every 4th to reduce test time)
    for (size_t i = 0; i < res.resultCount(); i += 4) {
      const Result& r = res.getResults()[i];
      if (!r.isValid()) continue;

      Position editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      Position bufferPos = test.toFullBufferPos(editPos);

      total++;
      const auto& seq = r.getSequenceString();

      // Build the full buffer with goal content for oracle simulation
      Lines fullWithGoal;
      for (int j = 0; j < numGoalLines; j++) {
        string line;
        if (j == 0) line += test.prefix;
        line += test.editRegion[j];
        if (j == numGoalLines - 1) line += test.suffix;
        fullWithGoal.push_back(line);
      }

      auto nvim = oracle->simulate(test.fullBuffer, bufferPos.line, bufferPos.col, seq.str());

      if (nvim.lines.flatten() == expected) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " editPos=[" << editPos.line << "," << editPos.col
               << "] bufferPos=[" << bufferPos.line << "," << bufferPos.col << "] seq='" << seq << "'\n"
               << "  FullBuffer: " << test.fullBuffer << "\n"
               << "  EditRegion: " << test.editRegion << "\n"
               << "  Goal: " << goal << "\n"
               << "  Expected: '" << expected << "'\n"
               << "  Got: '" << nvim.lines.flatten() << "'" << endl;
        }
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}
