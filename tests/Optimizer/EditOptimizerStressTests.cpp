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

  // Simple character set for readable test output
  static string randomWord(mt19937& rng, int len) {
    const string chars = "abcdef";
    string s;
    for (int i = 0; i < len; i++) s += chars[rng() % chars.size()];
    return s;
  }

  static Lines randomLines(mt19937& rng, int numLines, int minLen, int maxLen) {
    uniform_int_distribution<int> lenDist(minLen, maxLen);
    Lines lines;
    for (int i = 0; i < numLines; i++) {
      lines.push_back(randomWord(rng, lenDist(rng)));
    }
    return lines;
  }

  // Convert (row, col) to flat index (character count, not including newlines)
  static int toFlatIndex(int row, int col, const Lines& lines) {
    int idx = 0;
    for (int r = 0; r < row; r++) {
      idx += lines[r].empty() ? 1 : static_cast<int>(lines[r].size());
    }
    return idx + col;
  }

  // Convert flat index back to (row, col) position
  static Position fromFlatIndex(int flatIdx, const Lines& lines) {
    int remaining = flatIdx;
    for (int r = 0; r < static_cast<int>(lines.size()); r++) {
      int lineSize = lines[r].empty() ? 1 : static_cast<int>(lines[r].size());
      if (remaining < lineSize) {
        return Position(r, remaining);
      }
      remaining -= lineSize;
    }
    return Position(-1, -1);  // Invalid
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
    string prefix = randomWord(rng, 1 + rng() % 3);  // 1-3 chars
    string editContent = randomWord(rng, 3 + rng() % 6);  // 3-8 chars
    string suffix = randomWord(rng, 1 + rng() % 3);  // 1-3 chars
    Lines fullBuffer = {prefix + editContent + suffix};

    int startCol = static_cast<int>(prefix.size());
    int endCol = startCol + static_cast<int>(editContent.size()) - 1;
    Lines editRegion = {editContent};
    Lines target = {""};

    EditBoundary boundary(fullBuffer, {0, startCol}, {0, endCol});
    EditOptimizer opt = makeOptimizer();
    EditResult res = opt.optimizeEdit(editRegion, target, boundary);

    string expected = prefix + suffix;

    for (size_t i = 0; i < res.typeAllResults.size(); i++) {
      const Result& r = res.typeAllResults[i];
      if (!r.isValid()) continue;

      Position pos = fromFlatIndex(static_cast<int>(i), editRegion);
      int bufferCol = startCol + pos.col;

      total++;
      string seq = r.getSequenceString();
      auto result = oracle->simulate(fullBuffer, 0, bufferCol, seq);

      if (result.lines.flatten() == expected) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " col=" << pos.col << " seq='" << seq << "'\n"
               << "  FullBuffer: '" << fullBuffer[0] << "'\n"
               << "  EditRegion: '" << editContent << "' at [" << startCol << "," << endCol << "]\n"
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
    Lines source = randomLines(rng, 2 + rng() % 2, 4, 8);  // 2-3 lines
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
    string original = randomWord(rng, 5 + (rng() % 5));
    string replacement = randomWord(rng, original.size());
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
    // Generate edit region: 2-5 lines (increased range to stress test)
    int numLines = 2 + (rng() % 4);
    Lines editRegion = randomLines(rng, numLines, 3, 8);

    // Generate prefix (on first line) and suffix (on last line)
    // Increased range to stress test longer prefix/suffix content
    string prefix = randomWord(rng, 1 + rng() % 6);   // 1-6 chars
    string suffix = randomWord(rng, 1 + rng() % 6);   // 1-6 chars

    // Build full buffer: prefix + editRegion[0], editRegion[1..n-1], editRegion[n-1] + suffix
    Lines fullBuffer;
    for (int i = 0; i < numLines; i++) {
      string line = editRegion[i];
      if (i == 0) line = prefix + line;
      if (i == numLines - 1) line = line + suffix;
      fullBuffer.push_back(line);
    }

    // Edit region boundaries in full buffer coordinates
    int startLine = 0;
    int startCol = static_cast<int>(prefix.size());
    int endLine = numLines - 1;
    int endCol = static_cast<int>(fullBuffer[endLine].size()) - static_cast<int>(suffix.size()) - 1;

    Lines target = {""};

    EditBoundary boundary(fullBuffer, {startLine, startCol}, {endLine, endCol});
    EditOptimizer opt = makeOptimizer();
    EditResult res = opt.optimizeEdit(editRegion, target, boundary);

    // Expected result after deletion: prefix on line 0, suffix moved up
    string expected = prefix + suffix;

    // Test a subset of positions (every 2nd to reduce test time)
    for (size_t i = 0; i < res.typeAllResults.size(); i += 2) {
      const Result& r = res.typeAllResults[i];
      if (!r.isValid()) continue;

      Position pos = fromFlatIndex(static_cast<int>(i), editRegion);

      // Convert to full buffer coordinates
      int bufferLine = pos.line;
      int bufferCol = pos.col;
      if (pos.line == 0) {
        bufferCol += static_cast<int>(prefix.size());
      }

      total++;
      string seq = r.getSequenceString();
      auto nvim = oracle->simulate(fullBuffer, bufferLine, bufferCol, seq);

      if (nvim.lines.flatten() == expected) {
        passed++;
      } else {
        if (total - passed <= 3) {
          cerr << "FAIL iter=" << iter << " editPos=[" << pos.line << "," << pos.col
               << "] bufferPos=[" << bufferLine << "," << bufferCol << "] seq='" << seq << "'\n"
               << "  FullBuffer: " << fullBuffer << "\n"
               << "  EditRegion: " << editRegion << "\n"
               << "  Expected: '" << expected << "'\n"
               << "  Got: '" << nvim.lines.flatten() << "'" << endl;
        }
      }
    }
  }

  // With full prefix/suffix support, expect 100% pass rate
  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

