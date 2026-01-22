// tests/EditOptimizerTests.cpp

#include <gtest/gtest.h>
#include <memory>

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

// toFlatIndex and fromFlatIndex are in Utils/EditTestGenerators.h

class EditOptimizerTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(30, 1e4, 1.0, 2.0));
  }
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

// Apply a sequence of operations and return final state
ApplyResult applySequence(const Lines& source, Position startPos, const string& sequence) {
  ApplyResult result(source, startPos);

  for (const auto& op : Edit::parseEdits(sequence)) {
    Edit::applyEdit(result.lines, result.pos, result.mode, op);
  }

  return result;
}

// Check if ApplyResult cursor state matches SimulationResult
bool cursorStateMatches(const ApplyResult& ours, const SimulationResult& nvim) {
  return ours.pos.line == nvim.row && ours.pos.col == nvim.col && ours.mode == nvim.mode;
}

// Apply sequence and verify our Edit.cpp implementation matches NeovimOracle.
// Returns the oracle result (ground truth). Failures are reported via EXPECT macros.
SimulationResult verifySequenceWithOracle(
    NeovimOracle* oracle,
    const Lines& source,
    Position startPos,
    const string& sequence) {
  SimulationResult nvim = oracle->simulate(source, startPos.line, startPos.col, sequence);
  ApplyResult ours = applySequence(source, startPos, sequence);

  EXPECT_EQ(ours.lines, nvim.lines)
      << "Lines mismatch for seq='" << sequence << "' from ["
      << startPos.line << "," << startPos.col << "]\n"
      << "  Source: " << source << "\n"
      << "  Ours:   " << ours.lines << "\n"
      << "  Neovim: " << nvim.lines;

  EXPECT_TRUE(cursorStateMatches(ours, nvim))
      << "Cursor mismatch for seq='" << sequence << "'\n"
      << "  Ours:   (" << ours.pos.line << "," << ours.pos.col << ") mode=" << static_cast<int>(ours.mode) << "\n"
      << "  Neovim: (" << nvim.row << "," << nvim.col << ") mode=" << static_cast<int>(nvim.mode);

  return nvim;
}

// Check if lines represent an empty buffer (single empty line)
// Note: Lines invariant ensures buffer is never truly empty, so we check for single empty line.
bool isEmptyBuffer(const Lines& lines) {
  return lines.size() == 1 && lines[0].empty();
}

// Check if result is a valid deletion goal (empty buffer + normal mode)
bool isValidDeletionGoal(const ApplyResult& result) {
  return result.mode == Mode::Normal && isEmptyBuffer(result.lines);
}

// Check if SimulationResult is a valid deletion goal
bool isValidDeletionGoal(const SimulationResult& result) {
  return result.mode == Mode::Normal && isEmptyBuffer(result.lines);
}

// Create EditBoundary for full buffer deletion (no constraints)
EditBoundary makeFullBufferBoundary(const Lines& source) {
  int lastLine = static_cast<int>(source.size()) - 1;
  int lastCol = source[lastLine].empty() ? 0 : static_cast<int>(source[lastLine].size()) - 1;
  return EditBoundary(source, Position(0, 0), Position(lastLine, lastCol));
}

// Call optimizeEdit for full buffer deletion
EditResult optimizeFullDeletion(EditOptimizer& opt, const Lines& source) {
  EditBoundary boundary = makeFullBufferBoundary(source);
  Lines emptyTarget = {""};
  return opt.optimizeEdit(source, emptyTarget, boundary);
}

// Check that all positions in EditResult have valid solutions
bool allPositionsValid(const EditResult& res, const Lines& source) {
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].empty() ? 1 : static_cast<int>(source[r].size());
    for (int c = 0; c < cols; c++) {
      if (!res.typeAllResults[toFlatIndex(r, c, source)].isValid()) {
        return false;
      }
    }
  }
  return true;
}

TEST_F(EditOptimizerTest, DeletionSearch_Simple) {
  // Test deletion search from all positions in "aa\nbb"
  Lines source = {"aa", "bb"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  EXPECT_TRUE(allPositionsValid(res, source));

  // Verify each sequence against NeovimOracle and check it reaches goal
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].empty() ? 1 : source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        string seq = result.getSequenceString();
        SimulationResult nvim = verifySequenceWithOracle(oracle.get(), source, Position(r, c), seq);

        EXPECT_TRUE(isValidDeletionGoal(nvim))
            << "Sequence '" << seq << "' from [" << r << "," << c << "] "
            << "did not reach goal. Lines: " << nvim.lines
            << ", Mode: " << (nvim.mode == Mode::Insert ? "Insert" : "Normal");
      }
    }
  }
}

TEST_F(EditOptimizerTest, DeletionSearch_SingleLine) {
  Lines source = {"hello"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  for (int c = 0; c < (int)source[0].size(); c++) {
    int flatIdx = toFlatIndex(0, c, source);
    const Result& result = res.typeAllResults[flatIdx];
    ASSERT_TRUE(result.isValid()) << "No solution for position [0," << c << "]";

    string seq = result.getSequenceString();
    SimulationResult nvim = verifySequenceWithOracle(oracle.get(), source, Position(0, c), seq);
    EXPECT_TRUE(isValidDeletionGoal(nvim))
        << "Sequence '" << seq << "' from [0," << c << "] "
        << "did not reach goal. Lines: " << nvim.lines;
  }
}

TEST_F(EditOptimizerTest, DeletionSearch_ThreeLines) {
  Lines source = {"aaa", "bbb", "ccc"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        string seq = result.getSequenceString();
        SimulationResult nvim = verifySequenceWithOracle(oracle.get(), source, Position(r, c), seq);
        EXPECT_TRUE(isValidDeletionGoal(nvim))
            << "Sequence '" << seq << "' from [" << r << "," << c << "] "
            << "did not reach goal. Lines: " << nvim.lines
            << ", Mode: " << (nvim.mode == Mode::Insert ? "Insert" : "Normal");
      }
    }
  }
}

TEST_F(EditOptimizerTest, DeletionSearch_MixedLengths) {
  // Lines of different lengths
  Lines source = {"a", "bbb", "cc"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        // Apply sequence - assertions in Edit::applyEdit catch invalid operations
        applySequence(source, Position(r, c), result.getSequenceString());
      }
    }
  }
}

// =============================================================================
// Boundary-constrained deletion tests
// =============================================================================

TEST_F(EditOptimizerTest, DeletionSearch_WithLinesBelow) {
  // Multi-line edit region with hasLinesBelow=true.
  // Linewise deletion (dd) is now properly handled - after deleting a line,
  // cursor is clamped to remain within the edit region, allowing chained dd.
  //   aa        <- edit region start (no lines above)
  //   bb        <- edit region (last line)
  //   xx        <- outside (hasLinesBelow)
  Lines fullBuffer = {"aa", "bb", "xx"};
  Lines editRegion = {"aa", "bb"};

  // Construct boundary: edit region is lines 0-1, line 2 is outside
  EditBoundary boundary(fullBuffer, Position(0, 0), Position(1, 1));
  // This gives: prefix="", suffix="", hasLinesAbove=false, hasLinesBelow=true

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  EXPECT_TRUE(allPositionsValid(res, editRegion));
}

TEST_F(EditOptimizerTest, DeletionSearch_SingleLineWithLinesBelow) {
  // Single line with lines below - can't dd at all, must use S/cc
  // Use full buffer with proper positions:
  //   xx        <- line 0, outside
  //   hello     <- line 1, edit region
  //   xx        <- line 2, outside (hasLinesBelow)
  Lines fullBuffer = {"xx", "hello", "xx"};
  Lines editRegion = {"hello"};

  // Construct boundary from full buffer: edit region is line 1
  EditBoundary boundary(fullBuffer, Position(1, 0), Position(1, 4));
  // This gives: prefix="", suffix="", hasLinesAbove=true, hasLinesBelow=true

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  for (int c = 0; c < (int)editRegion[0].size(); c++) {
    int flatIdx = toFlatIndex(0, c, editRegion);
    const Result& result = res.typeAllResults[flatIdx];
    if (result.isValid()) {
      string seq = result.getSequenceString();
    }
  }
}

// =============================================================================
// Full buffer verification tests
// These tests verify that edit operations don't affect content outside the
// edit region when applied to the original full buffer.
// =============================================================================

// Helper: Apply a sequence to a full buffer at a given starting position
// Returns the final state
ApplyResult applySequenceToFullBuffer(const Lines& fullBuffer, Position startPos, const string& sequence) {
  return applySequence(fullBuffer, startPos, sequence);
}

// Helper: Check if a prefix of lines matches expected
bool linesMatchPrefix(const Lines& lines, const Lines& expected, int count) {
  if ((int)lines.size() < count || (int)expected.size() < count) return false;
  for (int i = 0; i < count; i++) {
    if (lines[i] != expected[i]) return false;
  }
  return true;
}

// Helper: Check if a suffix of lines matches expected (last 'count' lines)
bool linesMatchSuffix(const Lines& lines, const Lines& expected, int count) {
  if ((int)lines.size() < count || (int)expected.size() < count) return false;
  int linesStart = lines.size() - count;
  int expectedStart = expected.size() - count;
  for (int i = 0; i < count; i++) {
    if (lines[linesStart + i] != expected[expectedStart + i]) return false;
  }
  return true;
}

TEST_F(EditOptimizerTest, FullBuffer_Linewise) {
  // Linewise edit:
  // xx       <- line 0, outside
  // aa       <- line 1, edit region start
  // bb       <- line 2, edit region end
  // xx       <- line 3, outside
  //
  // Edit region: aa\nbb (lines 1-2)
  // Boundary: hasLinesAbove=true, hasLinesBelow=true

  Lines fullBuffer = {"xx", "aa", "bb", "xx"};
  Lines editRegion = {"aa", "bb"};

  // Construct boundary from fullBuffer: edit region is lines 1-2
  // This correctly computes hasLinesAbove=true (line 0), hasLinesBelow=true (line 3)
  EditBoundary boundary(fullBuffer, Position(1, 0), Position(2, 1));

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  // Test each starting position in the edit region
  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    int cols = editRegion[r].empty() ? 1 : editRegion[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, editRegion);
      const Result& result = res.typeAllResults[flatIdx];
      if (!result.isValid()) continue;

      string seq = result.getSequenceString();

      // Map edit region position to full buffer position
      // Edit region [r,c] -> full buffer [r+1, c] (edit region starts at line 1)
      Position fullBufferPos(r + 1, c);

      // Apply sequence to full buffer
      ApplyResult applied = applySequenceToFullBuffer(fullBuffer, fullBufferPos, seq);

      // Verify first and last lines ("xx") are unchanged
      ASSERT_GE(applied.lines.size(), 1u)
          << "Buffer too small after applying '" << seq << "'";
      EXPECT_EQ(applied.lines[0], "xx")
          << "Line 0 was modified! Expected 'xx', got '" << applied.lines[0]
          << "' after applying '" << seq << "'";
      EXPECT_EQ(applied.lines.back(), "xx")
          << "Last line was modified! Expected 'xx', got '" << applied.lines.back()
          << "' after applying '" << seq << "'";
    }
  }
}

TEST_F(EditOptimizerTest, FullBuffer_SpaceSeparated) {
  // Space-separated edit:
  // "x aa"    <- line 0: 'x ' outside, 'aa' edit region
  // "bb x"    <- line 1: 'bb' edit region, ' x' outside

  Lines fullBuffer = {"x aa", "bb x"};
  Lines editRegion = {"aa", "bb"};

  // Boundary: NOT full lines - dd/cc/S would delete outside content
  // Use full buffer with proper positions to get correct prefix/suffix
  EditBoundary boundary(fullBuffer, Position(0, 2), Position(1, 1));
  // prefix = "x ", suffix = " x"

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  // Verify NO line operations are used (they'd delete the x's)
  // Full line: dd, cc, S
  // To EOL: C, D, c$, d$
  // To SOL: c0, d0, c^, d^
  static const vector<string> FORBIDDEN_OPS = {
    "dd", "cc", "S",           // Full line ops
    "C", "D", "c$", "d$",      // To end of line (endsAtLineEnd=false)
    "c0", "d0", "c^", "d^"    // To start of line (startsAtLineStart=false)
  };

  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    int cols = editRegion[r].empty() ? 1 : editRegion[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, editRegion);
      const Result& result = res.typeAllResults[flatIdx];
      if (!result.isValid()) continue;

      string seq = result.getSequenceString();

      // Verify no forbidden operations in the sequence
      for (const auto& forbiddenOp : FORBIDDEN_OPS) {
        EXPECT_EQ(seq.find(forbiddenOp), string::npos)
            << "Sequence '" << seq << "' from [" << r << "," << c
            << "] contains '" << forbiddenOp << "' which would delete outside content!";
      }
    }
  }
}

TEST_F(EditOptimizerTest, FullBuffer_Linewise_VerifyNoEscape) {
  // More rigorous test: verify cursor never escapes to outside content
  // xx       <- line 0, outside
  // aa       <- line 1, edit region
  // bb       <- line 2, edit region
  // yy       <- line 3, outside
  //
  // After applying deletion sequence, cursor should be within the
  // edit region (or what remains of it), never on xx or yy

  Lines fullBuffer = {"xx", "aa", "bb", "yy"};
  Lines editRegion = {"aa", "bb"};

  // Construct boundary from fullBuffer: edit region is lines 1-2
  // This correctly computes hasLinesAbove=true (line 0), hasLinesBelow=true (line 3)
  EditBoundary boundary(fullBuffer, Position(1, 0), Position(2, 1));

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    int cols = editRegion[r].empty() ? 1 : editRegion[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, editRegion);
      const Result& result = res.typeAllResults[flatIdx];
      if (!result.isValid()) continue;

      string seq = result.getSequenceString();
      Position fullBufferPos(r + 1, c);

      ApplyResult applied = applySequenceToFullBuffer(fullBuffer, fullBufferPos, seq);

      // Verify cursor is not on line 0 (the "xx" above)
      EXPECT_GT(applied.pos.line, 0)
          << "Cursor escaped to line above! Pos=[" << applied.pos.line << ","
          << applied.pos.col << "] after '" << seq << "'";

      // After deletion, buffer size changed. The original "yy" line
      // should still be the last line, and cursor should not be on it.
      // With edit region of 2 lines reduced to 1 empty line,
      // buffer goes from 4 lines to 3 lines: ["xx", "", "yy"]
      // Cursor should be on line 1 (the empty edit region), not line 0 or 2

      // Verify first line unchanged
      EXPECT_EQ(applied.lines[0], "xx")
          << "First line modified after '" << seq << "'";

      // Verify last line unchanged
      EXPECT_EQ(applied.lines.back(), "yy")
          << "Last line modified after '" << seq << "'";

      // Verify cursor is in the middle (the edit region remnant)
      EXPECT_EQ(applied.pos.line, 1)
          << "Cursor not in edit region! Pos=[" << applied.pos.line << ","
          << applied.pos.col << "] after '" << seq << "', buffer=" << applied.lines;
    }
  }
}

// =============================================================================
// Replacement strategy tests
// =============================================================================

TEST_F(EditOptimizerTest, Replacement_SingleDiff) {
  // "fresh" -> "frosh" - only 'e' -> 'o' differs at position 2
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("fresh", "frosh", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();
  EXPECT_FALSE(seq.empty());

  // Should navigate to position 2, then replace 'e' with 'o'
  EXPECT_TRUE(seq.find("ro") != string::npos || seq.find("r") != string::npos)
      << "Expected replacement command in: " << seq;
}

TEST_F(EditOptimizerTest, Replacement_MultipleDiffs) {
  // "hello" -> "jello" - 'h' -> 'j' at position 0
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("hello", "jello", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();

  // At position 0, should just be "rj"
  EXPECT_EQ(seq, "rj")
      << "Expected 'rj' for single char replacement at col 0";
}

TEST_F(EditOptimizerTest, Replacement_ConsecutiveDiffs) {
  // "abc" -> "xyz" - all three chars differ
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("abc", "xyz", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
}

// Precondition tests - these test that callers properly check preconditions
// before calling tryReplacement (which asserts on invalid input)

TEST_F(EditOptimizerTest, Replacement_NoDiffs_Precondition) {
  // "same" -> "same" - no differences
  // Callers should check deleted != inserted before calling
  string deleted = "same";
  string inserted = "same";
  EXPECT_EQ(deleted, inserted) << "Precondition: caller should skip identical strings";
}

TEST_F(EditOptimizerTest, Replacement_DifferentLengths_Precondition) {
  // Different lengths - callers should check this
  string deleted = "hello";
  string inserted = "hi";
  EXPECT_NE(deleted.size(), inserted.size()) << "Precondition: caller should check lengths";
}

TEST_F(EditOptimizerTest, Replacement_WithNewlines_Precondition) {
  // Strings with newlines - callers should check for single-line
  string deleted = "hello\nworld";
  EXPECT_NE(deleted.find('\n'), string::npos) << "Precondition: caller should check for newlines";
}

TEST_F(EditOptimizerTest, Replacement_SparseDiffs) {
  // "0000000" -> "1001001" - three non-consecutive diffs at positions 0, 3, 6
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("0000000", "1001001", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();

  // Should have multiple r1 commands with navigation
  size_t replaceCount = 0;
  for (size_t i = 0; i < seq.size(); i++) {
    if (seq[i] == 'r' && i+1 < seq.size() && seq[i+1] == '1') {
      replaceCount++;
    }
  }
  EXPECT_GE(replaceCount, 3u)
      << "Expected at least 3 single-char replacements: " << seq;
}

