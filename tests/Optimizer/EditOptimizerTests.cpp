// tests/EditOptimizerTests.cpp

#include <gtest/gtest.h>
#include <memory>
#include <random>

#include "Editor/Edit.h"
#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class EditOptimizerTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  EditOptimizer opt{config, OptimizerParams(30, 1e4, 1.0, 2.0)};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> EditOptimizerTest::oracle;

// =============================================================================
// Test Helpers
// =============================================================================

struct ApplyResult {
  Lines lines;
  Position pos;
  Mode mode;

  ApplyResult(Lines lines, Position pos, Mode mode = Mode::Normal)
      : lines(std::move(lines)), pos(pos), mode(mode) {}
};

ApplyResult applySequence(const Lines& source, Position startPos, const string& sequence) {
  ApplyResult result(source, startPos);
  for (const auto& op : Edit::parseEdits(sequence)) {
    Edit::applyEdit(result.lines, result.pos, result.mode, op);
  }
  return result;
}

bool cursorStateMatches(const ApplyResult& ours, const SimulationResult& nvim) {
  return ours.pos.line == nvim.row && ours.pos.col == nvim.col && ours.mode == nvim.mode;
}

SimulationResult verifySequenceWithOracle(
    NeovimOracle* oracle,
    const Lines& source,
    Position startPos,
    const string& sequence) {
  SimulationResult nvim = oracle->simulate(source, startPos.line, startPos.col, sequence);
  ApplyResult ours = applySequence(source, startPos, sequence);

  EXPECT_EQ(ours.lines, nvim.lines)
      << "Lines mismatch for seq='" << sequence << "' from " << startPos << "\n"
      << "  Source: " << source << "\n"
      << "  Ours:   " << ours.lines << "\n"
      << "  Neovim: " << nvim.lines;

  EXPECT_TRUE(cursorStateMatches(ours, nvim))
      << "Cursor mismatch for seq='" << sequence << "'\n"
      << "  Ours:   " << ours.pos << " mode=" << static_cast<int>(ours.mode) << "\n"
      << "  Neovim: (" << nvim.row << "," << nvim.col << ") mode=" << static_cast<int>(nvim.mode);

  return nvim;
}

bool allPositionsValid(const vector<Result>& results, const Lines& source) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    for (int c = 0; c < source[r].effectiveSize(); c++) {
      if (!results[idx++].isValid()) {
        return false;
      }
    }
  }
  return true;
}

template<typename Fn>
void forEachValidResult(const vector<Result>& results, const Lines& lines, Fn fn) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(lines.size()); r++) {
    for (int c = 0; c < lines[r].effectiveSize(); c++) {
      const Result& result = results[idx++];
      if (result.isValid()) {
        fn(Position(r, c), result.getSequenceString());
      }
    }
  }
}

// =============================================================================
// Pure Deletion Tests (full buffer, no boundaries)
// =============================================================================

TEST_F(EditOptimizerTest, PureDeletion_OracleVerified) {
  // Single test with oracle verification - stress tests cover more shapes
  Lines lines = {"aa", "bb"};
  vector<Result> res = opt.optimizePureDeletion(lines, EditBoundary(lines, Position(0, 0), lines.lastPos()));

  EXPECT_TRUE(allPositionsValid(res, lines));

  forEachValidResult(res, lines, [&](Position pos, const string& seq) {
    SimulationResult nvimRes = verifySequenceWithOracle(oracle.get(), lines, pos, seq);
    EXPECT_TRUE(nvimRes.lines.isEmpty() && nvimRes.mode == Mode::Normal)
        << "Sequence '" << seq << "' from " << pos << " did not reach goal";
  });
}

// =============================================================================
// Boundary-Constrained Tests
// =============================================================================

TEST_F(EditOptimizerTest, Boundary_LinesBelow) {
  // Edit region has lines below - tests hasLinesBelow constraint
  Lines fullBuffer = {"aa", "bb", "xx"};
  Position startPos(0, 0), endPos(1, 1);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);
  EXPECT_TRUE(allPositionsValid(res.typeAllResults, editRegion));
}

TEST_F(EditOptimizerTest, Boundary_SingleLineSurrounded) {
  // Single line edit region surrounded by other lines
  // Can't use dd - must use S/cc
  Lines fullBuffer = {"xx", "hello", "xx"};
  Position startPos(1, 0), endPos(1, 4);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);
  EXPECT_TRUE(allPositionsValid(res.typeAllResults, editRegion));
}

TEST_F(EditOptimizerTest, Boundary_PrefixSuffix) {
  // Edit region has prefix and suffix on same lines
  // "x aa"    <- 'x ' is prefix, 'aa' is edit region
  // "bb x"    <- 'bb' is edit region, ' x' is suffix
  Lines fullBuffer = {"x aa", "bb x"};
  Position startPos(0, 2), endPos(1, 1);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);

  // These operations would delete prefix/suffix content
  static const vector<string> FORBIDDEN_OPS = {
    "dd", "cc", "S",           // Full line ops
    "C", "D", "c$", "d$",      // To end of line
    "c0", "d0", "c^", "d^"     // To start of line
  };

  forEachValidResult(res.typeAllResults, editRegion, [&](Position pos, const string& seq) {
    for (const auto& forbiddenOp : FORBIDDEN_OPS) {
      EXPECT_EQ(seq.find(forbiddenOp), string::npos)
          << "Sequence '" << seq << "' from " << pos
          << " contains '" << forbiddenOp << "' which would delete outside content!";
    }
  });
}

TEST_F(EditOptimizerTest, Boundary_LinewiseCursorContainment) {
  // Verify cursor stays within edit region and surrounding lines unchanged
  Lines fullBuffer = {"xx", "aa", "bb", "yy"};
  Position startPos(1, 0), endPos(2, 1);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);

  forEachValidResult(res.typeAllResults, editRegion, [&](Position pos, const string& seq) {
    Position fullBufferPos(pos.line + startPos.line, pos.col);
    ApplyResult applied = applySequence(fullBuffer, fullBufferPos, seq);

    // Verify surrounding lines unchanged
    EXPECT_EQ(applied.lines[0], "xx") << "Line above modified after '" << seq << "'";
    EXPECT_EQ(applied.lines.back(), "yy") << "Line below modified after '" << seq << "'";

    // Verify cursor stayed in edit region (line 1 after deletion)
    EXPECT_EQ(applied.pos.line, 1)
        << "Cursor escaped edit region! Pos=" << applied.pos << " after '" << seq << "'";
  });
}

// =============================================================================
// Replacement Strategy Tests
// =============================================================================

TEST_F(EditOptimizerTest, Replacement_SingleChar) {
  // "hello" -> "jello" - single char at position 0
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("hello", "jello", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  EXPECT_EQ(results[0].getSequenceString(), "rj");
}

TEST_F(EditOptimizerTest, Replacement_SingleCharMiddle) {
  // "fresh" -> "frosh" - single char at position 2
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("fresh", "frosh", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  string seq = results[0].getSequenceString();
  EXPECT_TRUE(seq.find("ro") != string::npos) << "Expected 'ro' in: " << seq;
}

TEST_F(EditOptimizerTest, Replacement_ConsecutiveChars) {
  // "abc" -> "xyz" - all three chars differ, should use R mode
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("abc", "xyz", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  EXPECT_TRUE(results[0].isValid());
}

TEST_F(EditOptimizerTest, Replacement_SparseChars) {
  // "0000000" -> "1001001" - three non-consecutive diffs
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("0000000", "1001001", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  string seq = results[0].getSequenceString();

  // Count r1 occurrences
  size_t replaceCount = 0;
  for (size_t i = 0; i + 1 < seq.size(); i++) {
    if (seq[i] == 'r' && seq[i + 1] == '1') replaceCount++;
  }
  EXPECT_GE(replaceCount, 3u) << "Expected at least 3 'r1' in: " << seq;
}


// =============================================================================
// Stress Tests (random buffers, oracle-verified)
// =============================================================================

// Single-line embedded: PREFIX | EDIT_REGION | SUFFIX on one line
// Boundary checking works correctly since positions don't shift across lines
TEST_F(EditOptimizerTest, Stress_SingleLineEmbedded) {
  const int NUM_ITERATIONS = 50;
  mt19937 rng(42);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    auto test = generateRandomSingleLineEmbedded(rng);
    EditResult res = opt.optimizeEdit(test.editRegion, {""}, test.makeBoundary());
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
TEST_F(EditOptimizerTest, Stress_MultiLineFullBuffer) {
  oracle->restart();
  const int NUM_ITERATIONS = 30;
  mt19937 rng(43);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int numLines = 2 + rng() % 2;  // 2-3 lines
    Lines source = randomLines(rng, numLines, 4, 8);
    EditBoundary boundary(source, {0, 0}, source.lastPos());
    EditResult res = opt.optimizeEdit(source, {""}, boundary);

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

TEST_F(EditOptimizerTest, DISABLED_Stress_Replacement_SameLength) {
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
TEST_F(EditOptimizerTest, Stress_MultiLineEmbedded) {
  oracle->restart();
  const int NUM_ITERATIONS = 50;
  mt19937 rng(45);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    auto test = generateRandomMultiLineEmbedded(rng);
    EditResult res = opt.optimizeEdit(test.editRegion, {""}, test.makeBoundary());
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
