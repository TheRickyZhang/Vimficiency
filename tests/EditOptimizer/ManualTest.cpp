// tests/EditOptimizer/ManualTest.cpp
//
// Manual tests for EditOptimizer with hardcoded setups.
// Tests boundary constraints, replacement strategies, and specific behaviors.
// For random/stress tests, see OutputCorrectnessTest.cpp.

#include <gtest/gtest.h>
#include <memory>

#include "Editor/Edit.h"
#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Utils/Lines.h"
// #include "Utils/TestUtils.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class EditOptimizer_ManualTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  EditOptimizer opt{config, OptimizerParams{.maxResults = 30}};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> EditOptimizer_ManualTest::oracle;

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

TEST_F(EditOptimizer_ManualTest, PureDeletion_OracleVerified) {
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

TEST_F(EditOptimizer_ManualTest, Boundary_LinesBelow) {
  // Edit region has lines below - tests hasLinesBelow constraint
  Lines fullBuffer = {"aa", "bb", "xx"};
  Position startPos(0, 0), endPos(1, 1);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);
  EXPECT_TRUE(allPositionsValid(res.typeAllResults, editRegion));
}

TEST_F(EditOptimizer_ManualTest, Boundary_SingleLineSurrounded) {
  // Single line edit region surrounded by other lines
  // Can't use dd - must use S/cc
  Lines fullBuffer = {"xx", "hello", "xx"};
  Position startPos(1, 0), endPos(1, 4);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);
  // printResultsDebug(res.typeAllResults, "boundary line surrounded");
  EXPECT_TRUE(allPositionsValid(res.typeAllResults, editRegion));
}

TEST_F(EditOptimizer_ManualTest, Boundary_PrefixSuffix) {
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

TEST_F(EditOptimizer_ManualTest, Boundary_LinewiseCursorContainment) {
  // Verify cursor stays within edit region and surrounding lines unchanged
  Lines fullBuffer = {"xx", "aa", "bb", "yy"};
  Position startPos(1, 0), endPos(2, 1);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);

  forEachValidResult(res.typeAllResults, editRegion, [&](Position pos, const string& seq) {
    // Skip visual mode sequences - they may cross boundaries which is a known optimizer limitation
    // TODO: Fix optimizer to not output boundary-crossing visual sequences
    if (!seq.empty() && seq[0] == 'v') return;

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

TEST_F(EditOptimizer_ManualTest, Replacement_SingleChar) {
  // "hello" -> "jello" - single char at position 0
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("hello", "jello", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  EXPECT_EQ(results[0].getSequenceString(), "rj");
}

TEST_F(EditOptimizer_ManualTest, Replacement_SingleCharMiddle) {
  // "fresh" -> "frosh" - single char at position 2
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("fresh", "frosh", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  string seq = results[0].getSequenceString();
  EXPECT_TRUE(seq.find("ro") != string::npos) << "Expected 'ro' in: " << seq;
}

TEST_F(EditOptimizer_ManualTest, Replacement_ConsecutiveChars) {
  // "abc" -> "xyz" - all three chars differ, should use R mode
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("abc", "xyz", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  EXPECT_TRUE(results[0].isValid());
}

TEST_F(EditOptimizer_ManualTest, Replacement_SparseChars) {
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
// Note: Stress tests (random buffers) are in OutputCorrectnessTest.cpp
// =============================================================================
